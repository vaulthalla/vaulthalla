'use client'

import React from 'react'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { Badge } from '@/components/ui/Badge'
import { Meter } from '@/components/ui/Stat'
import { Tooltip } from '@/components/ui/Tooltip'
import { DASH, formatDateTime, formatPercent, formatRelative, titleCase } from '@/lib/format'
import { providerLabel, scopeLabel, windowLabel, type BudgetTrend, type LedgerEntry } from '@/features/cost/model'
import { EmptyRows, Money } from '@/features/cost/bits'

const When = ({ value }: { value: string | null }) =>
  value ?
    <span className="tabular text-fg-subtle whitespace-nowrap" title={formatDateTime(value)}>
      {formatRelative(value)}
    </span>
  : <span className="text-fg-faint">{DASH}</span>

export const trendTarget = (
  t: BudgetTrend,
  vaultName: (id: number | null) => string | null,
  credentialName?: (id: number | null) => string | null,
) => {
  switch (t.scope) {
    case 'provider':
      return providerLabel(t.provider_key) ?? 'Provider'
    case 'vault':
      return vaultName(t.vault_id) ?? 'Vault'
    case 'gateway_credential':
      return credentialName?.(t.gateway_credential_id) ?? `Gateway key ${t.gateway_credential_id ?? ''}`
    case 'gateway_credential_vault':
      return `${credentialName?.(t.gateway_credential_id) ?? 'Gateway key'} on ${vaultName(t.vault_id) ?? 'vault'}`
    default:
      return 'All S3 storage'
  }
}

export const TrendTable = ({
  trends,
  vaultName,
  credentialName,
  empty,
}: {
  trends: BudgetTrend[]
  vaultName: (id: number | null) => string | null
  credentialName?: (id: number | null) => string | null
  empty?: React.ReactNode
}) => {
  const columns: Column<BudgetTrend>[] = [
    {
      key: 'target',
      header: 'Budget',
      cell: t => (
        <div>
          <div className="text-fg font-medium">{trendTarget(t, vaultName, credentialName)}</div>
          <div className="text-fg-subtle text-xs">
            {scopeLabel(t.scope)} · {windowLabel(t.window_type)}
          </div>
        </div>
      ),
    },
    {
      key: 'used',
      header: 'Used',
      sortValue: t => t.percent_used,
      className: 'min-w-44',
      cell: t => (
        <div className="space-y-1.5">
          <div className="flex items-baseline justify-between gap-3 text-xs">
            <span className="text-fg">
              <Money value={t.total_cost} currency={t.currency} /> <span className="text-fg-subtle">of</span>{' '}
              <Money value={t.limit} currency={t.currency} />
            </span>
            <span className="tabular text-fg-subtle">{formatPercent(t.percent_used, { digits: 0 })}</span>
          </div>
          <Meter
            ratio={t.percent_used}
            tone="accent"
            label={`${trendTarget(t, vaultName, credentialName)} budget used`}
          />
        </div>
      ),
    },
    {
      key: 'projected',
      header: 'Projected',
      hideBelow: 'md',
      cell: t => (
        <div className="text-xs">
          <Money value={t.projected_window_cost} currency={t.currency} />
          {t.projected_overage && Number(t.projected_overage) > 0 ?
            <div className="text-fg-subtle">
              over by <Money value={t.projected_overage} currency={t.currency} />
            </div>
          : null}
        </div>
      ),
    },
    { key: 'exhaustion', header: 'Runs out', hideBelow: 'lg', cell: t => <When value={t.predicted_exhaustion_at} /> },
    {
      key: 'confidence',
      header: 'Confidence',
      hideBelow: 'lg',
      cell: t =>
        t.confidence && t.confidence !== 'none' ?
          <Badge>{titleCase(t.confidence)}</Badge>
        : <Tooltip content="Not enough recorded spend to project this window yet.">
            <span className="text-fg-faint">{DASH}</span>
          </Tooltip>,
    },
  ]
  if (trends.length === 0)
    return <EmptyRows>{empty ?? 'No budget has a spend limit yet, so there are no budget windows to track.'}</EmptyRows>
  return (
    <DataTable
      rows={trends}
      columns={columns}
      rowKey={t =>
        `${t.scope}:${t.policy_id ?? ''}:${t.provider_key ?? ''}:${t.vault_id ?? ''}:${t.gateway_credential_id ?? ''}:${t.window_type}`
      }
      empty={empty ?? 'No budget has a spend limit yet, so there are no budget windows to track.'}
    />
  )
}

export const LedgerTable = ({
  ledger,
  vaultName,
  gateway = false,
  empty,
}: {
  ledger: LedgerEntry[]
  vaultName: (id: number | null) => string | null
  gateway?: boolean
  empty?: React.ReactNode
}) => {
  const columns: Column<LedgerEntry>[] = [
    { key: 'when', header: 'When', sortValue: e => e.created_at, cell: e => <When value={e.created_at} /> },
    {
      key: 'what',
      header: gateway ? 'Operation' : 'Vault',
      cell: e =>
        gateway ?
          <div className="min-w-0">
            <div className="text-fg font-mono text-xs">{e.operation ?? DASH}</div>
            {e.object_key ?
              <div className="text-fg-subtle max-w-[16rem] truncate font-mono text-xs" title={e.object_key}>
                {e.object_key}
              </div>
            : null}
          </div>
        : <div>
            <div className="text-fg">{vaultName(e.vault_id) ?? DASH}</div>
            <div className="text-fg-subtle text-xs">{providerLabel(e.provider_key) ?? DASH}</div>
          </div>,
    },
    ...(gateway ?
      [
        {
          key: 'vault',
          header: 'Vault',
          hideBelow: 'md' as const,
          cell: (e: LedgerEntry) => <span>{vaultName(e.vault_id) ?? DASH}</span>,
        },
      ]
    : [
        {
          key: 'window',
          header: 'Window',
          hideBelow: 'md' as const,
          cell: (e: LedgerEntry) => <span className="text-fg-muted">{windowLabel(e.window)}</span>,
        },
      ]),
    {
      key: 'cost',
      header: 'Cost',
      sortValue: e => Number(e.committed_cost ?? e.reserved_cost ?? NaN),
      cell: e => (
        <div className="text-xs">
          <Money
            value={e.committed_cost ?? e.reserved_cost}
            currency={e.currency}
            className="tabular text-fg text-sm"
          />
          <div className="text-fg-subtle">
            {e.committed_cost !== null ?
              'committed'
            : e.reserved_cost !== null ?
              'reserved'
            : ''}
          </div>
        </div>
      ),
    },
    {
      key: 'status',
      header: 'Status',
      cell: e => (e.status ? <Badge>{titleCase(e.status)}</Badge> : <span className="text-fg-faint">{DASH}</span>),
    },
    {
      key: 'source',
      header: 'Source',
      hideBelow: 'lg',
      cell: e => (
        <span className="text-fg-subtle text-xs">
          {e.usage_source ? titleCase(e.usage_source) : DASH}
          {e.synthetic ? ' · synthetic' : ''}
        </span>
      ),
    },
  ]
  if (ledger.length === 0)
    return (
      <EmptyRows>
        {empty
          ?? 'Nothing recorded yet. Budgets in report, warn or enforce mode record each sync’s estimated cost here.'}
      </EmptyRows>
    )
  return (
    <DataTable
      rows={ledger}
      columns={columns}
      rowKey={e => e.id ?? `${e.run_uuid}-${e.request_uuid}-${e.created_at}`}
      initialSort={{ key: 'when', dir: 'desc' }}
    />
  )
}
