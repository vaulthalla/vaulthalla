'use client'

import React, { useState } from 'react'
import { api } from '@/lib/session'
import { Panel, DefinitionList } from '@/components/ui/Panel'
import { Button } from '@/components/ui/Button'
import { Field, Select, Textarea } from '@/components/ui/Field'
import { Badge } from '@/components/ui/Badge'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { EmptyState, InlineError } from '@/components/ui/State'
import { StatGrid, StatTile } from '@/components/ui/Stat'
import { notify } from '@/components/ui/Toast'
import { ArrowsRotateIcon, FlaskIcon, HandIcon } from '@/components/ui/icons'
import { DASH, formatDuration, formatInt, titleCase } from '@/lib/format'
import { toPreflight, windowLabel, scopeLabel, type PreflightResult, type WindowCheck } from '@/features/cost/model'
import { refreshPricing, type VaultLite } from '@/features/cost/queries'
import { Money } from '@/features/cost/bits'

const PLAN_LABELS: Record<string, string> = {
  upload: 'Uploads',
  download: 'Downloads',
  index_remote_only: 'Index only',
  delete_remote: 'Remote deletes',
  delete_local: 'Local deletes',
}

const yesNo = (value: boolean | null) =>
  value === null ? DASH
  : value ? 'Yes'
  : 'No'

const ChecksTable = ({ checks }: { checks: WindowCheck[] }) => {
  const columns: Column<WindowCheck>[] = [
    {
      key: 'budget',
      header: 'Budget',
      cell: c => (
        <div>
          <div className="text-fg">{scopeLabel(c.scope)}</div>
          <div className="text-fg-subtle text-xs">
            {windowLabel(c.window)} · {c.mode ? titleCase(c.mode) : DASH}
          </div>
        </div>
      ),
    },
    {
      key: 'used',
      header: 'Used before',
      hideBelow: 'sm',
      cell: c => <Money value={c.used_before} currency={c.currency} />,
    },
    { key: 'requested', header: 'This run', cell: c => <Money value={c.requested} currency={c.currency} /> },
    { key: 'limit', header: 'Limit', cell: c => <Money value={c.limit} currency={c.currency} /> },
    {
      key: 'result',
      header: 'Result',
      cell: c =>
        c.exceeded ?
          <Badge tone={c.mode === 'enforce' ? 'danger' : 'warn'}>Over limit</Badge>
        : <Badge>Within limit</Badge>,
    },
  ]
  return (
    <DataTable
      rows={checks}
      columns={columns}
      rowKey={c => `${c.policy_id}-${c.window}`}
      empty="No budget applies to this vault, so nothing was checked."
    />
  )
}

const Decision = ({ result }: { result: PreflightResult }) => {
  const { decision } = result
  if (decision.allowed === null) return <Badge tone="unknown">Unknown</Badge>
  if (decision.stalled || !decision.allowed)
    return (
      <Badge tone="danger" dot>
        Would be blocked
      </Badge>
    )
  if (decision.warnings.length)
    return (
      <Badge tone="warn" dot>
        Allowed with warnings
      </Badge>
    )
  return (
    <Badge tone="ok" dot>
      Allowed
    </Badge>
  )
}

const OverrideRequest = ({
  vault,
  result,
  onDone,
}: {
  vault: VaultLite
  result: PreflightResult
  onDone: () => void
}) => {
  const [reason, setReason] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)
  const blocking = result.decision.checks.filter(c => c.exceeded && c.mode === 'enforce' && c.policy_id !== null)

  const request = async () => {
    setBusy(true)
    setError(null)
    try {
      await api.send('pricing.budget.override.request', {
        vault_id: vault.id,
        reason: reason.trim() || null,
        policy_ids: blocking.map(c => c.policy_id as number),
        estimated_cost: result.estimate.estimated_cost,
        currency: result.estimate.currency ?? result.decision.currency,
      })
      await refreshPricing()
      notify.success('Override requested', 'A super admin can approve it under Overview → Override requests.')
      setReason('')
      onDone()
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const retry = async () => {
    try {
      const res = await api.send('storage.vault.sync', { id: vault.id })
      notify.success(res.status === 'rerun_queued' ? 'Sync queued after the current run' : 'Sync started', vault.name)
    } catch (err) {
      notify.error(err, 'Could not start the sync')
    }
  }

  return (
    <Panel
      title="This sync would be blocked"
      description={
        result.decision.reason
        ?? 'The estimated cost exceeds an enforced budget. Request a one-time override, or raise the budget.'
      }
      className="border-danger-line">
      <div className="space-y-3">
        <Field label="Reason for the override" htmlFor="override-reason" hint="Shown to the approver.">
          <Textarea id="override-reason" value={reason} onChange={event => setReason(event.target.value)} rows={3} />
        </Field>
        <InlineError error={error} />
        <div className="flex flex-wrap gap-2">
          <Button variant="primary" onClick={request} loading={busy} disabled={blocking.length === 0}>
            <HandIcon aria-hidden />
            Request override
          </Button>
          <Button variant="secondary" onClick={retry}>
            <ArrowsRotateIcon aria-hidden />
            Retry sync
          </Button>
        </div>
      </div>
    </Panel>
  )
}

export const PreflightTab = ({ s3Vaults, vaultsPending }: { s3Vaults: VaultLite[]; vaultsPending: boolean }) => {
  const [vaultId, setVaultId] = useState<number | null>(null)
  const [result, setResult] = useState<{ vaultId: number; data: PreflightResult } | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)
  const selected = s3Vaults.find(v => v.id === (vaultId ?? s3Vaults[0]?.id)) ?? null

  const run = async () => {
    if (!selected) return
    setBusy(true)
    setError(null)
    setResult(null)
    try {
      const res = await api.send('pricing.budget.preflight', { vault_id: selected.id })
      setResult({ vaultId: selected.id, data: toPreflight(res) })
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  if (!vaultsPending && s3Vaults.length === 0)
    return (
      <EmptyState
        className="panel"
        icon={FlaskIcon}
        title="No S3 vaults"
        description="A dry run estimates what the next sync of an S3 vault would cost."
      />
    )

  const current = result && result.vaultId === selected?.id ? result.data : null
  const est = current?.estimate

  return (
    <div className="space-y-5">
      <Panel
        title="Dry run"
        description="Plans the next sync of a vault from its remote index and checks the estimated cost against every budget, without syncing.">
        <div className="flex flex-wrap items-end gap-3">
          <Field label="Vault" htmlFor="preflight-vault" className="w-full sm:w-72">
            <Select
              id="preflight-vault"
              value={selected?.id ?? ''}
              onChange={event => {
                setVaultId(Number(event.target.value))
                setError(null)
              }}>
              {s3Vaults.map(v => (
                <option key={v.id} value={v.id}>
                  {v.name}
                </option>
              ))}
            </Select>
          </Field>
          <Button variant="primary" onClick={run} loading={busy} disabled={!selected}>
            <FlaskIcon aria-hidden />
            Run dry run
          </Button>
        </div>
        {error ?
          <InlineError error={error} className="mt-4" />
        : null}
      </Panel>

      {current && est && selected ?
        <>
          <Panel
            title={
              <span className="flex items-center gap-3">
                Result <Decision result={current} />
              </span>
            }>
            <StatGrid className="xl:grid-cols-4">
              <StatTile
                label="Estimated cost"
                value={est.estimated_cost ? <Money value={est.estimated_cost} currency={est.currency} /> : null}
                hint={est.unavailable_reason ?? undefined}
              />
              {Object.entries(PLAN_LABELS).map(([key, label]) => (
                <StatTile
                  key={key}
                  label={label}
                  value={current.plan[key] === undefined ? null : formatInt(current.plan[key])}
                />
              ))}
            </StatGrid>
            {current.decision.warnings.length ?
              <ul className="rounded-control border-warn-line bg-warn-soft text-warn mt-4 space-y-1 border px-3 py-2 text-sm">
                {current.decision.warnings.map(w => (
                  <li key={w}>{w}</li>
                ))}
              </ul>
            : null}
            <DefinitionList
              className="mt-5"
              items={[
                [
                  'Price catalog',
                  est.catalog_version ? <span className="font-mono text-xs">{est.catalog_version}</span> : DASH,
                ],
                ['Catalog verified', yesNo(est.catalog_verified)],
                ['Catalog age', formatDuration(est.catalog_age_seconds)],
                ['Confidence', est.confidence_level ? titleCase(est.confidence_level) : DASH],
                ['Unknown costs', est.unknowns.length ? est.unknowns.join(', ') : 'None'],
              ]}
            />
          </Panel>
          <ChecksTable checks={current.decision.checks} />
          {current.decision.stalled || current.decision.allowed === false ?
            <OverrideRequest vault={selected} result={current} onDone={() => undefined} />
          : null}
        </>
      : null}
    </div>
  )
}
