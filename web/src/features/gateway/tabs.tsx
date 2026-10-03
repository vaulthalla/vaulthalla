'use client'

import React, { useMemo, useState } from 'react'
import { api } from '@/lib/session'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { Badge } from '@/components/ui/Badge'
import { Button } from '@/components/ui/Button'
import { Field, Input, Select } from '@/components/ui/Field'
import { Panel } from '@/components/ui/Panel'
import { EmptyState, QueryState } from '@/components/ui/State'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { BucketIcon, CloudIcon, LinkIcon, PlusIcon, TrashIcon } from '@/components/ui/icons'
import { DASH, formatDateTime, formatRelative } from '@/lib/format'
import { EmptyRows, Section } from '@/features/cost/bits'
import { LedgerTable, TrendTable } from '@/features/cost/tables'
import { LimitsSummary, ModeBadge } from '@/features/cost/bits'
import type { BudgetPolicy } from '@/features/cost/model'
import {
  BUCKET_MODES,
  suggestedEndpoint,
  type BucketBinding,
  type GatewayCredential,
  type GatewayStatus,
} from '@/features/gateway/model'
import {
  refreshBuckets,
  useBuckets,
  useGatewayBudgetStatus,
  useGatewayCredentials,
  useGatewayLedger,
  useGatewayPerms,
  useGatewayPolicies,
  useVaultOptions,
} from '@/features/gateway/queries'
import type { BucketDialogKind } from '@/features/gateway/BucketDialogs'
import { CopyField } from '@/features/gateway/CopyField'

// ---- buckets ----------------------------------------------------------------------------------------------------

const unbind = async (b: BucketBinding) => {
  const ok = await confirm({
    title: `Unbind bucket “${b.bucket_name}”?`,
    description: 'Clients can no longer reach the vault under this bucket name. The vault and its files stay.',
    confirmLabel: 'Unbind',
  })
  if (!ok) return
  try {
    await api.send('s3.gateway.buckets.unbind', { bucket_name: b.bucket_name })
    await refreshBuckets()
    notify.success('Bucket unbound', b.bucket_name)
  } catch (error) {
    notify.error(error, 'Could not unbind the bucket')
  }
}

export const BucketsTab = ({ onDialog }: { onDialog: (kind: BucketDialogKind) => void }) => {
  const can = useGatewayPerms()
  const buckets = useBuckets()
  const vaults = useVaultOptions()
  const columns: Column<BucketBinding>[] = [
    {
      key: 'bucket',
      header: 'Bucket',
      sortValue: b => b.bucket_name,
      cell: b => (
        <span className="text-fg font-mono text-sm" data-testid="s3-gateway-bucket-name">
          {b.bucket_name}
        </span>
      ),
    },
    {
      key: 'vault',
      header: 'Vault',
      sortValue: b => vaults.name(b.vault_id),
      cell: b => <span className="text-fg-muted">{vaults.name(b.vault_id) ?? DASH}</span>,
    },
    {
      key: 'mode',
      header: 'Mode',
      cell: b =>
        b.mode ? <Badge>{BUCKET_MODES[b.mode]?.label ?? b.mode}</Badge> : <span className="text-fg-faint">{DASH}</span>,
    },
    {
      key: 'exclusive',
      header: 'Gateway only',
      hideBelow: 'md',
      cell: b => (
        <span className="text-fg-subtle">
          {b.api_exclusive === null ?
            DASH
          : b.api_exclusive ?
            'Yes'
          : 'No'}
        </span>
      ),
    },
    {
      key: 'created',
      header: 'Bound',
      hideBelow: 'lg',
      sortValue: b => b.created_at,
      cell: b => (
        <span className="tabular text-fg-subtle" title={formatDateTime(b.created_at)}>
          {formatRelative(b.created_at)}
        </span>
      ),
    },
  ]
  return (
    <Section
      id="buckets"
      title="Buckets"
      description={
        <span data-testid="s3-gateway-bucket-routing-note">
          Each bucket name maps to one vault. Bindings do not grant access: a key still needs its principal’s
          permissions or a gateway vault role.
        </span>
      }
      actions={
        can.buckets ?
          <>
            <Button size="sm" variant="secondary" onClick={() => onDialog('bind')}>
              <LinkIcon aria-hidden />
              Bind vault
            </Button>
            <Button size="sm" variant="secondary" onClick={() => onDialog('remote')}>
              <CloudIcon aria-hidden />
              Remote-cache bucket
            </Button>
            <Button
              size="sm"
              variant="secondary"
              data-testid="s3-gateway-open-local-bucket"
              onClick={() => onDialog('local')}>
              <PlusIcon aria-hidden />
              Local bucket
            </Button>
          </>
        : null
      }>
      <div data-testid="s3-gateway-section-bucket-bindings-routing">
        <QueryState query={buckets}>
          {rows =>
            rows.length === 0 ?
              <EmptyState
                className="panel"
                icon={BucketIcon}
                title="No buckets yet"
                description="Create a local bucket, or bind an existing vault, so S3 clients have somewhere to read and write."
              />
            : <DataTable
                rows={rows}
                columns={columns}
                rowKey={b => b.bucket_name}
                initialSort={{ key: 'bucket', dir: 'asc' }}
                filter={(b, q) => `${b.bucket_name} ${vaults.name(b.vault_id) ?? ''}`.toLowerCase().includes(q)}
                filterPlaceholder="Filter buckets…"
                rowActions={b =>
                  can.buckets ?
                    <Button
                      size="icon-sm"
                      variant="ghost"
                      aria-label={`Unbind ${b.bucket_name}`}
                      onClick={() => void unbind(b)}>
                      <TrashIcon aria-hidden />
                    </Button>
                  : null
                }
              />
          }
        </QueryState>
      </div>
    </Section>
  )
}

// ---- budgets overview -------------------------------------------------------------------------------------------

interface PolicyRow {
  policy: BudgetPolicy
  credential: GatewayCredential | null
}

export const BudgetsTab = ({ onOpenCredential }: { onOpenCredential: (c: GatewayCredential) => void }) => {
  const policies = useGatewayPolicies()
  const status = useGatewayBudgetStatus()
  const ledger = useGatewayLedger()
  const credentials = useGatewayCredentials()
  const vaults = useVaultOptions()
  const byId = useMemo(() => new Map((credentials.data ?? []).map(c => [c.id, c])), [credentials.data])
  const credentialName = (id: number | null) => (id ? (byId.get(id)?.name ?? `Key ${id}`) : null)
  const rows: PolicyRow[] = (policies.data ?? [])
    .filter(p => p.is_active)
    .map(p => ({ policy: p, credential: p.gateway_credential_id ? (byId.get(p.gateway_credential_id) ?? null) : null }))
  const columns: Column<PolicyRow>[] = [
    {
      key: 'key',
      header: 'Key',
      cell: r => (
        <div className="py-1.5">
          <div className="text-fg font-medium">{credentialName(r.policy.gateway_credential_id) ?? DASH}</div>
          <div className="text-fg-subtle text-xs">
            {r.policy.scope === 'gateway_credential_vault' ? `On ${vaults.name(r.policy.vault_id)}` : 'Whole key'}
          </div>
        </div>
      ),
    },
    { key: 'mode', header: 'Mode', headerClassName: 'w-32', cell: r => <ModeBadge policy={r.policy} /> },
    { key: 'limits', header: 'Limits', hideBelow: 'sm', cell: r => <LimitsSummary policy={r.policy} /> },
  ]
  return (
    <div className="space-y-8" data-testid="s3-gateway-section-budget-overview">
      <Section
        id="gw-budgets"
        title="Key budgets"
        description="Caps on what requests through a gateway key may cost. Open a key to change its caps.">
        <QueryState query={policies}>
          {() =>
            rows.length === 0 ?
              <EmptyRows>No gateway key has a budget. Open a key under Keys to set one.</EmptyRows>
            : <DataTable
                rows={rows}
                columns={columns}
                rowKey={r => r.policy.id ?? `${r.policy.scope}-${r.policy.vault_id}`}
                onRowClick={r => r.credential && onOpenCredential(r.credential)}
              />
          }
        </QueryState>
      </Section>
      <Section id="gw-windows" title="Budget windows">
        <QueryState query={status}>
          {data => (
            <TrendTable
              trends={data.trends}
              vaultName={vaults.name}
              credentialName={credentialName}
              empty="No key budget has a spend window yet."
            />
          )}
        </QueryState>
      </Section>
      <Section id="gw-ledger" title="Ledger" description="Recorded gateway request charges, newest first.">
        <QueryState query={ledger}>
          {rows => (
            <LedgerTable gateway ledger={rows} vaultName={vaults.name} empty="No gateway charges recorded yet." />
          )}
        </QueryState>
      </Section>
    </div>
  )
}

// ---- client setup -----------------------------------------------------------------------------------------------

const Snippet = ({ title, text }: { title: string; text: string }) => (
  <div className="rounded-card border-line min-w-0 overflow-hidden border bg-black/30">
    <div className="border-line flex items-center justify-between border-b px-3 py-2">
      <span className="text-fg-muted text-xs font-medium">{title}</span>
      <Button
        size="sm"
        variant="ghost"
        onClick={() =>
          void navigator.clipboard?.writeText(text).then(
            () => notify.success('Copied'),
            () => notify.info('Copy it manually'),
          )
        }>
        Copy
      </Button>
    </div>
    <pre className="text-fg overflow-x-auto px-3 py-2.5 font-mono text-xs leading-relaxed whitespace-pre">{text}</pre>
  </div>
)

export const ClientSetupTab = ({ status }: { status: GatewayStatus | null }) => {
  const credentials = useGatewayCredentials()
  const buckets = useBuckets()
  const [endpoint, setEndpoint] = useState<string | null>(null)
  const [keyId, setKeyId] = useState('')
  const suggested = suggestedEndpoint(status, typeof window === 'undefined' ? '' : window.location.hostname)
  const url = (endpoint ?? suggested) || 'http://<gateway-host>:39000'
  const active = (credentials.data ?? []).filter(c => c.enabled)
  const key = active.find(c => String(c.id) === keyId) ?? active[0] ?? null
  const accessKey = key?.access_key ?? '<access-key-id>'
  const bucket = buckets.data?.[0]?.bucket_name ?? '<bucket>'
  return (
    <div className="space-y-5" data-testid="s3-gateway-section-client-setup">
      <Panel
        title="Client setup"
        description="Any S3 client works. Use path-style addressing and the secret you saved when the key was created.">
        <div className="grid gap-4 sm:grid-cols-2">
          <Field
            label="Endpoint URL"
            htmlFor="gw-endpoint"
            hint={
              status && !endpoint ?
                'Suggested from the gateway’s port and this browser’s host. Use whatever address clients actually reach.'
              : undefined
            }>
            <Input
              id="gw-endpoint"
              value={url}
              onChange={e => setEndpoint(e.target.value)}
              className="font-mono"
              spellCheck={false}
            />
          </Field>
          <Field label="Key" htmlFor="gw-snippet-key">
            <Select
              id="gw-snippet-key"
              value={key ? String(key.id) : ''}
              onChange={e => setKeyId(e.target.value)}
              disabled={active.length === 0}>
              {active.length === 0 ?
                <option value="">No active keys</option>
              : null}
              {active.map(c => (
                <option key={c.id} value={c.id}>
                  {c.name}
                </option>
              ))}
            </Select>
          </Field>
        </div>
        {key ?
          <CopyField label="Access key ID" value={key.access_key} />
        : null}
      </Panel>
      <div className="grid gap-4 lg:grid-cols-2">
        <Snippet
          title="Environment"
          text={`export AWS_ACCESS_KEY_ID=${accessKey}\nexport AWS_SECRET_ACCESS_KEY=<secret-access-key>\nexport AWS_EC2_METADATA_DISABLED=true`}
        />
        <Snippet
          title="AWS CLI"
          text={`aws configure set s3.addressing_style path\naws --endpoint-url ${url} s3 ls\naws --endpoint-url ${url} s3 cp ./backup.tar s3://${bucket}/backup.tar`}
        />
        <Snippet
          title="MinIO client (mc)"
          text={`mc alias set vaulthalla ${url} ${accessKey} <secret-access-key>\nmc ls vaulthalla/${bucket}`}
        />
        <Snippet
          title="rclone"
          text={`[vaulthalla]\ntype = s3\nprovider = Other\nendpoint = ${url}\naccess_key_id = ${accessKey}\nsecret_access_key = <secret-access-key>\nforce_path_style = true`}
        />
      </div>
    </div>
  )
}
