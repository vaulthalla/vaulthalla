'use client'

import React from 'react'
import Link from 'next/link'
import { useWs } from '@/lib/query'
import { Badge } from '@/components/ui/Badge'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { StatGrid, StatTile } from '@/components/ui/Stat'
import { formatBytes, formatInt, titleCase } from '@/lib/format'
import { detailPoll, useHealthCards } from '@/features/health/hooks'
import { Fact, HealthSection, SubHeading } from '@/features/health/components'
import { BarList } from '@/features/health/charts'
import { statusTone } from '@/features/health/model'
import { bool, bytes, int, list, num, obj, pair, percent, seconds, text, type Raw } from '@/features/health/read'

const CARDS = ['system.storage', 'system.db', 'system.retention']
const right = { className: 'text-right', headerClassName: 'text-right' }
const muted = (value: React.ReactNode) => <span className="text-fg-faint">{value}</span>

const vaultColumns: Column<Raw>[] = [
  {
    key: 'name',
    header: 'Vault',
    cell: v => (
      <Link href={`/vaults/${v.vault_id}`} className="font-medium text-fg hover:text-accent-text">
        {text(v.vault_name) ?? `Vault ${v.vault_id}`}
      </Link>
    ),
    sortValue: v => text(v.vault_name),
  },
  { key: 'type', header: 'Backend', cell: v => <span className="uppercase text-fg-muted">{text(v.type) ?? 'unknown'}</span>, sortValue: v => text(v.type) },
  {
    key: 'status',
    header: 'Status',
    cell: v => (
      <Badge tone={statusTone(text(v.backend_status))} dot>
        {titleCase(text(v.backend_status) ?? 'unknown')}
      </Badge>
    ),
  },
  { key: 'active', header: 'Active', cell: v => <Fact value={bool(v.active)} yes="Active" no="Inactive" />, hideBelow: 'md' },
  { key: 'size', header: 'Stored', cell: v => <span className="tabular">{bytes(v.vault_size_bytes) ?? muted('—')}</span>, sortValue: v => num(v.vault_size_bytes), ...right },
  {
    key: 'quota',
    header: 'Quota',
    cell: v => <span className="tabular">{num(v.quota_bytes) === 0 ? muted('none') : (bytes(v.quota_bytes) ?? muted('—'))}</span>,
    hideBelow: 'sm',
    ...right,
  },
  {
    key: 'free',
    header: 'Free',
    cell: v => (
      <span className="tabular" title={bool(v.min_free_space_ok) === false ? 'Below the minimum free space' : undefined}>
        {bytes(v.free_space_bytes) ?? muted('—')}
        {bool(v.min_free_space_ok) === false ? <span className="ml-1 text-warn">below minimum</span> : null}
      </span>
    ),
    sortValue: v => num(v.free_space_bytes),
    ...right,
  },
  {
    key: 'bucket',
    header: 'Bucket',
    cell: v => (text(v.bucket) ? <span className="font-mono text-xs">{text(v.bucket)}</span> : muted('—')),
    hideBelow: 'lg',
  },
  {
    key: 'enc',
    header: 'Upstream encryption',
    cell: v => (text(v.type) === 's3' ? <Fact value={bool(v.upstream_encryption_enabled)} yes="On" no="Off" /> : muted('—')),
    hideBelow: 'lg',
  },
]

const StorageBody = ({ stats }: { stats: Raw }) => (
  <div className="space-y-4">
    <StatGrid className="xl:grid-cols-6">
      <StatTile label="Vaults" value={int(stats.vault_count_total)} />
      <StatTile label="Active" value={int(stats.active_vault_count)} />
      <StatTile label="Inactive" value={int(stats.inactive_vault_count)} />
      <StatTile label="Degraded" value={int(stats.degraded_vault_count)} />
      <StatTile label="In error" value={int(stats.error_vault_count)} />
      <StatTile label="Local / S3" value={pair(stats.local_vault_count, stats.s3_vault_count)} />
    </StatGrid>
    <DataTable rows={list(stats.vaults)} columns={vaultColumns} rowKey={v => String(v.vault_id)} empty="No vaults are configured." />
    <p className="text-xs text-fg-subtle">Provider operation counts and latency are not instrumented yet.</p>
  </div>
)

const DbBody = ({ stats }: { stats: Raw }) => {
  const tables = list(stats.largest_tables)
  const statements = bool(stats.pg_stat_statements_enabled)
  return (
    <div className="space-y-6">
      {text(stats.error) ? (
        <p className="rounded-control border border-danger-line bg-danger-soft px-3 py-2 font-mono text-xs text-danger">{text(stats.error)}</p>
      ) : null}
      <StatGrid className="xl:grid-cols-5">
        <StatTile label="Connected" value={<Fact value={bool(stats.connected)} yes="Yes" no="No" />} hint={text(stats.database_name) ?? undefined} />
        <StatTile label="Size" value={bytes(stats.db_size_bytes)} />
        <StatTile label="Connections" value={pair(stats.connections_total, stats.connections_max)} hint={num(stats.connections_max) === null ? 'limit not reported' : 'in use / max'} />
        <StatTile label="Active / idle" value={pair(stats.connections_active, stats.connections_idle)} />
        <StatTile label="Idle in transaction" value={int(stats.connections_idle_in_transaction)} />
        <StatTile label="Cache hit ratio" value={percent(stats.cache_hit_ratio, 2)} />
        <StatTile label="Oldest transaction" value={seconds(stats.oldest_transaction_age_seconds)} />
        <StatTile label="Deadlocks" value={int(stats.deadlocks)} />
        <StatTile label="Temp files" value={bytes(stats.temp_bytes)} />
        <StatTile
          label="Slow queries"
          value={int(stats.slow_query_count)}
          hint={statements === false ? 'needs pg_stat_statements' : undefined}
        />
      </StatGrid>
      <div>
        <SubHeading>Largest tables</SubHeading>
        <BarList
          emptyLabel="No table sizes reported."
          items={tables.map(t => ({
            key: text(t.table_name) ?? '',
            label: <span className="font-mono text-xs">{(text(t.table_name) ?? 'unknown').replace(/^public\./, '')}</span>,
            value: num(t.total_bytes),
            display: (
              <span>
                {formatBytes(t.total_bytes)}
                <span className="ml-2 text-xs font-normal text-fg-subtle">~{formatInt(t.row_estimate)} rows</span>
              </span>
            ),
          }))}
        />
      </div>
    </div>
  )
}

interface RetentionRow {
  key: string
  label: string
  items: React.ReactNode
  overdue: React.ReactNode
  policy: React.ReactNode
  oldest: React.ReactNode
}

const retentionColumns: Column<RetentionRow>[] = [
  { key: 'label', header: 'Data', cell: r => <span className="font-medium text-fg">{r.label}</span> },
  { key: 'items', header: 'Kept', cell: r => <span className="tabular">{r.items}</span>, ...right },
  { key: 'overdue', header: 'Past retention', cell: r => <span className="tabular">{r.overdue}</span>, ...right },
  { key: 'policy', header: 'Policy', cell: r => <span className="text-fg-muted">{r.policy}</span>, hideBelow: 'md' },
  { key: 'oldest', header: 'Oldest', cell: r => <span className="tabular">{r.oldest}</span>, hideBelow: 'sm', ...right },
]

const days = (v: unknown) => (num(v) === null ? null : `${formatInt(v)} days`)
const orDash = (v: React.ReactNode) => v ?? muted('—')
const withBytes = (count: unknown, size: unknown) => (num(count) === null ? null : `${formatInt(count)}${num(size) !== null ? ` · ${formatBytes(size)}` : ''}`)

const RetentionBody = ({ stats }: { stats: Raw }) => {
  const rows: RetentionRow[] = [
    {
      key: 'trash',
      label: 'Trash',
      items: orDash(withBytes(stats.trashed_files_count, stats.trashed_bytes_total)),
      overdue: orDash(withBytes(stats.trashed_files_past_retention_count, stats.trashed_bytes_past_retention)),
      policy: orDash(days(stats.trash_retention_days)),
      oldest: num(stats.trashed_files_count) === 0 ? muted('none') : orDash(seconds(stats.oldest_trashed_age_seconds)),
    },
    {
      key: 'sync',
      label: 'Sync events',
      items: orDash(int(stats.sync_events_total)),
      overdue: orDash(int(stats.sync_events_past_retention_count)),
      policy: orDash(
        days(stats.sync_event_retention_days) &&
          `${days(stats.sync_event_retention_days)}${num(stats.sync_event_max_entries) ? `, max ${formatInt(stats.sync_event_max_entries)}` : ''}`,
      ),
      oldest: muted('—'),
    },
    {
      key: 'audit',
      label: 'Audit log',
      items: orDash(int(stats.audit_log_entries_total)),
      overdue: orDash(int(stats.audit_log_entries_past_retention_count)),
      policy: orDash(days(stats.audit_log_retention_days)),
      oldest: num(stats.audit_log_entries_total) === 0 ? muted('none') : orDash(seconds(stats.oldest_audit_log_age_seconds)),
    },
    {
      key: 'share',
      label: 'Share access events',
      items: orDash(int(stats.share_access_events_total)),
      overdue: muted('—'),
      policy: muted('no separate policy'),
      oldest: num(stats.share_access_events_total) === 0 ? muted('none') : orDash(seconds(stats.oldest_share_access_event_age_seconds)),
    },
    {
      key: 'cache',
      label: 'Cache index',
      items: orDash(withBytes(stats.cache_entries_total, stats.cache_bytes_total)),
      overdue: orDash(
        int(stats.cache_entries_expired) && `${int(stats.cache_entries_expired)} expired · ${int(stats.cache_eviction_candidates) ?? '—'} to evict`,
      ),
      policy: orDash(
        days(stats.cache_expiry_days) &&
          `${days(stats.cache_expiry_days)}${num(stats.cache_max_size_bytes) ? `, max ${formatBytes(stats.cache_max_size_bytes)}` : ''}`,
      ),
      oldest: muted('—'),
    },
  ]
  return (
    <div className="space-y-3">
      <DataTable rows={rows} columns={retentionColumns} rowKey={r => r.key} />
    </div>
  )
}

export function StoragePage() {
  const { cards, pending } = useHealthCards(CARDS)
  const storage = useWs('stats.system.storage', null, detailPoll)
  const db = useWs('stats.system.db', null, detailPoll)
  const retention = useWs('stats.system.retention', null, detailPoll)
  return (
    <div className="space-y-5">
      <HealthSection id="storage-backend" title="Storage backends" card={cards.get('system.storage')} cardPending={pending} query={storage}>
        {data => <StorageBody stats={obj(data.stats)} />}
      </HealthSection>
      <HealthSection id="database" title="Database" card={cards.get('system.db')} cardPending={pending} query={db}>
        {data => <DbBody stats={obj(data.stats)} />}
      </HealthSection>
      <HealthSection id="retention" title="Retention & cleanup" card={cards.get('system.retention')} cardPending={pending} query={retention}>
        {data => <RetentionBody stats={obj(data.stats)} />}
      </HealthSection>
    </div>
  )
}
