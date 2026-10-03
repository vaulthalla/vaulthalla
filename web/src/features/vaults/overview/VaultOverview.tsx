'use client'

import React, { useState } from 'react'
import { useWs } from '@/lib/query'
import { Segmented } from '@/components/ui/Tabs'
import {
  formatBytes,
  formatDateTime,
  formatDuration,
  formatInt,
  formatMoney,
  formatRelative,
  titleCase,
} from '@/lib/format'
import { STATS_POLL_MS, useCurrentVault, useVaultBackend } from '@/features/vaults/VaultShell'
import { realTimestamp } from '@/features/vaults/model'
import { Metric, MetricGrid, MiniList, NA, Note, StatPanel, yesNo } from '@/features/vaults/overview/blocks'
import { ProportionBar, SplitBar, TrendChart } from '@/features/vaults/overview/charts'
import type {
  ActivityStats,
  OperationStats,
  PricingStats,
  RecoveryStats,
  RetentionStats,
  SecurityStats,
  ShareStats,
  SyncHealthStats,
  TrendsStats,
  VaultStatsPayload,
} from '@/features/vaults/overview/types'

// The vault's health at a glance. One cached query per stats command, polled every 15 s while this tab is mounted
// and visible (TanStack pauses hidden tabs and stops when the tab unmounts). Severities are the backend's.

const poll = { refetchInterval: STATS_POLL_MS, retry: false } as const

const days = (n: number | null | undefined) => (typeof n === 'number' ? `${formatInt(n)} d` : null)
const ago = (value: number | string | null | undefined) => (realTimestamp(value) === null ? null : formatRelative(value))
const count = (n: number | null | undefined) => (typeof n === 'number' ? formatInt(n) : null)
const bytes = (n: number | null | undefined) => (typeof n === 'number' ? formatBytes(n) : null)

export const VaultOverview = () => {
  const vault = useCurrentVault()
  const vault_id = vault.id
  const base = useWs('stats.vault', { vault_id }, { ...poll, select: d => d.stats as unknown as VaultStatsPayload })
  const sync = useWs('stats.vault.sync', { vault_id }, { ...poll, select: d => d.stats as unknown as SyncHealthStats })
  const recovery = useWs('stats.vault.recovery', { vault_id }, { ...poll, select: d => d.stats as unknown as RecoveryStats })
  const security = useWs('stats.vault.security', { vault_id }, { ...poll, select: d => d.stats as unknown as SecurityStats })
  const activity = useWs('stats.vault.activity', { vault_id }, { ...poll, select: d => d.stats as unknown as ActivityStats })
  const shares = useWs('stats.vault.shares', { vault_id }, { ...poll, select: d => d.stats as unknown as ShareStats })
  const operations = useWs('stats.vault.operations', { vault_id }, { ...poll, select: d => d.stats as unknown as OperationStats })
  const retention = useWs('stats.vault.retention', { vault_id }, { ...poll, select: d => d.stats as unknown as RetentionStats })
  const pricing = useWs('stats.vault.pricing', { vault_id }, { ...poll, select: d => d.stats as unknown as PricingStats })
  const backend = useVaultBackend(vault_id)

  return (
    <div className="grid gap-4 lg:grid-cols-2 xl:grid-cols-3">
      <StatPanel title="Capacity" query={base} className="lg:col-span-2">
        {data => <Capacity stats={data} quota={vault.quota} />}
      </StatPanel>

      <StatPanel title="Sync health" query={sync} status={d => d.overall_status} link={{ href: `/vaults/${vault_id}/sync`, label: 'Sync settings' }}>
        {s => (
          <>
            <MetricGrid>
              <Metric label="State" value={s.current_state ? titleCase(s.current_state) : null} />
              <Metric label="Last success" value={ago(s.last_success_at)} hint={realTimestamp(s.last_success_at) ? formatDateTime(s.last_success_at) : undefined} />
              <Metric label="Interval" value={typeof s.sync_interval_seconds === 'number' ? formatDuration(s.sync_interval_seconds) : null} hint={s.sync_enabled === false ? 'Sync disabled' : undefined} />
              <Metric label="Errors 24h" value={count(s.error_count_24h)} hint={s.error_count_7d != null ? `${formatInt(s.error_count_7d)} in 7 d` : undefined} />
              <Metric label="Failed 24h" value={count(s.failed_ops_24h)} hint={s.retry_count_24h != null ? `${formatInt(s.retry_count_24h)} retries` : undefined} />
              <Metric label="Conflicts" value={count(s.conflict_count_open)} />
            </MetricGrid>
            <div className="mt-5">
              <h3 className="mb-2 text-[11px] font-medium tracking-wide text-fg-subtle uppercase">Traffic, last 24 h</h3>
              <SplitBar
                format={bytes}
                parts={[
                  { label: 'Up', value: s.bytes_up_24h, tone: 'accent' },
                  { label: 'Down', value: s.bytes_down_24h, tone: 'neutral' },
                ]}
              />
            </div>
            {s.last_error_message ? <Note tone="danger">{s.last_error_code ? `${s.last_error_code}: ` : ''}{s.last_error_message}</Note> : null}
            {s.last_stall_reason ? <Note tone="danger">Stalled: {s.last_stall_reason}</Note> : null}
            {s.s3_budget_warning ? <Note tone="warn">{s.s3_budget_warning}</Note> : null}
          </>
        )}
      </StatPanel>

      <StatPanel title="Recovery readiness" query={recovery} status={d => d.recovery_readiness}>
        {r => (
          <>
            <MetricGrid>
              <Metric label="Backup" value={r.backup_status ? titleCase(r.backup_status) : null} />
              <Metric label="Policy" value={r.backup_policy_present === false ? 'None' : yesNo(r.backup_enabled) === 'Yes' ? 'Enabled' : yesNo(r.backup_enabled) ? 'Disabled' : null} />
              <Metric label="Interval" value={r.backup_interval_seconds ? formatDuration(r.backup_interval_seconds) : null} />
              <Metric label="Last backup" value={ago(r.last_backup_at)} />
              <Metric label="Next due" value={realTimestamp(r.next_expected_backup_at) ? formatDateTime(r.next_expected_backup_at) : null} />
              <Metric label="Missed" value={count(r.missed_backup_count_estimate)} />
            </MetricGrid>
            {r.backup_policy_present === false ? <Note>No backup policy is configured for this vault.</Note> : null}
            {r.last_error ? <Note tone="danger">{r.last_error}</Note> : null}
          </>
        )}
      </StatPanel>

      <StatPanel title="Security" query={security} status={d => d.overall_status}>
        {s => (
          <>
            <MetricGrid>
              <Metric label="Encryption" value={s.encryption_status ? titleCase(s.encryption_status) : null} />
              <Metric label="Key version" value={s.current_key_version != null ? `v${s.current_key_version}` : null} hint={days(s.key_age_days) ? `${days(s.key_age_days)} old` : undefined} />
              <Metric label="Integrity" value={s.integrity_check_status && s.integrity_check_status !== 'not_available' ? titleCase(s.integrity_check_status) : null} hint={ago(s.last_integrity_check_at) ?? undefined} />
              <Metric label="Current key" value={count(s.files_current_key_version)} hint={s.files_legacy_key_version ? `${formatInt(s.files_legacy_key_version)} on older keys` : undefined} />
              <Metric label="Denied 24h" value={count(s.unauthorized_access_attempts_24h)} hint={ago(s.last_denied_access_at) ? `last ${ago(s.last_denied_access_at)}` : undefined} />
              <Metric label="Throttled 24h" value={count(s.rate_limited_attempts_24h)} />
            </MetricGrid>
            {s.last_denied_access_reason ? <Note>Last refusal: {s.last_denied_access_reason}</Note> : null}
          </>
        )}
      </StatPanel>

      <StatPanel title="Operation queue" query={operations} status={d => d.overall_status}>
        {o => (
          <>
            <MetricGrid>
              <Metric label="Pending" value={count(o.pending_operations)} hint={o.oldest_pending_operation_age_seconds != null ? `oldest ${formatDuration(o.oldest_pending_operation_age_seconds)}` : undefined} />
              <Metric label="In progress" value={count(o.in_progress_operations)} />
              <Metric label="Stalled" value={count(o.stalled_operations)} />
              <Metric label="Failed 24h" value={count(o.failed_operations_24h)} hint={o.cancelled_operations_24h ? `${formatInt(o.cancelled_operations_24h)} cancelled` : undefined} />
              <Metric label="Uploads" value={count(o.active_share_uploads)} hint={o.stalled_share_uploads ? `${formatInt(o.stalled_share_uploads)} stalled` : undefined} />
              <Metric
                label="Upload bytes"
                value={o.upload_bytes_expected_active ? `${formatBytes(o.upload_bytes_received_active)} / ${formatBytes(o.upload_bytes_expected_active)}` : o.upload_bytes_expected_active === 0 ? 'Idle' : null}
              />
            </MetricGrid>
            {(o.recent_operation_errors ?? []).length ? (
              <MiniList
                title="Recent errors"
                empty=""
                items={(o.recent_operation_errors ?? []).slice(0, 4).map((e, i) => ({
                  key: `${i}`,
                  primary: e.error || titleCase(e.status ?? 'error'),
                  secondary: `${titleCase(e.operation ?? 'operation')} · ${e.path ?? e.target ?? ''}`,
                  meta: ago(e.occurred_at),
                }))}
              />
            ) : null}
          </>
        )}
      </StatPanel>

      <StatPanel title="Activity" query={activity} className="lg:col-span-2" link={{ href: `/files/${vault_id}`, label: 'Open files' }}>
        {a => <Activity stats={a} />}
      </StatPanel>

      <StatPanel title="Share links" query={shares} link={{ href: `/vaults/${vault_id}/shares`, label: 'Manage links' }}>
        {s => (
          <>
            <MetricGrid>
              <Metric label="Active" value={count(s.active_links)} hint={s.links_created_24h ? `${formatInt(s.links_created_24h)} new today` : undefined} />
              <Metric label="Public" value={count(s.public_links)} />
              <Metric label="Verified" value={count(s.email_validated_links)} />
              <Metric label="Downloads" value={count(s.downloads_24h)} />
              <Metric label="Uploads" value={count(s.uploads_24h)} />
              <Metric
                label="Denied 24h"
                value={count(s.denied_attempts_24h)}
                hint={[s.failed_attempts_24h ? `${formatInt(s.failed_attempts_24h)} failed` : '', s.rate_limited_attempts_24h ? `${formatInt(s.rate_limited_attempts_24h)} rate limited` : ''].filter(Boolean).join(' · ') || undefined}
              />
            </MetricGrid>
            {(s.top_links_by_access ?? []).length ? (
              <MiniList
                title="Most opened"
                empty=""
                items={(s.top_links_by_access ?? []).slice(0, 3).map((l, i) => ({
                  key: l.share_id ?? `${i}`,
                  primary: l.label || l.root_path || 'Link',
                  secondary: l.root_path ?? undefined,
                  meta: `${formatInt(l.access_count)} opens`,
                }))}
              />
            ) : null}
          </>
        )}
      </StatPanel>

      <StatPanel title="Storage backend" query={backend} status={d => d?.backend_status}>
        {b =>
          b ? (
            <>
              <MetricGrid>
                <Metric label="Vault size" value={bytes(b.vault_size_bytes)} />
                <Metric label="Cache" value={bytes(b.cache_size_bytes)} />
                <Metric label="Free space" value={bytes(b.free_space_bytes)} hint={b.min_free_space_ok === false ? 'below the minimum' : undefined} />
                <Metric label="Latency" value={b.provider_latency_avg_ms != null ? `${formatInt(b.provider_latency_avg_ms)} ms` : null} hint={b.provider_latency_max_ms != null ? `max ${formatInt(b.provider_latency_max_ms)} ms` : undefined} />
                <Metric label="Errors" value={count(b.provider_errors_total)} hint={b.provider_ops_total != null ? `of ${formatInt(b.provider_ops_total)} ops` : undefined} />
                <Metric label="Last success" value={ago(b.last_provider_success_at)} />
              </MetricGrid>
              {b.last_provider_error ? <Note tone="danger">{b.last_provider_error}</Note> : null}
            </>
          ) : (
            <p className="text-sm text-fg-faint">The server reported no backend for this vault.</p>
          )
        }
      </StatPanel>

      <StatPanel title="Retention" query={retention} status={d => d.cleanup_status}>
        {r => (
          <MetricGrid>
            <Metric label="In trash" value={count(r.trashed_files_count)} hint={bytes(r.trashed_bytes_total) ?? undefined} />
            <Metric label="Overdue" value={count(r.trashed_files_past_retention_count)} hint={days(r.trash_retention_days) ? `kept ${days(r.trash_retention_days)}` : undefined} />
            <Metric label="Cached" value={count(r.cache_entries_total)} hint={r.cache_entries_expired ? `${formatInt(r.cache_entries_expired)} expired` : undefined} />
            <Metric label="Cache size" value={bytes(r.cache_bytes_total)} hint={r.cache_max_size_bytes ? `of ${formatBytes(r.cache_max_size_bytes)}` : undefined} />
            <Metric label="Sync events" value={count(r.sync_events_total)} hint={r.sync_events_past_retention_count ? `${formatInt(r.sync_events_past_retention_count)} past retention` : days(r.sync_event_retention_days) ? `kept ${days(r.sync_event_retention_days)}` : undefined} />
            <Metric label="Audit entries" value={count(r.audit_log_entries_total)} hint={r.audit_log_entries_past_retention_count ? `${formatInt(r.audit_log_entries_past_retention_count)} past retention` : undefined} />
          </MetricGrid>
        )}
      </StatPanel>

      <StatPanel title="Cost this month" query={pricing} link={{ href: `/cost?vault=${vault_id}`, label: 'Cost control' }}>
        {p => {
          const currency = p.currency || 'USD'
          return (
            <MetricGrid>
              <Metric label="Spend" value={p.current_monthly_spend != null ? formatMoney(p.current_monthly_spend, currency) : null} />
              <Metric label="Projected" value={p.projected_monthly_spend != null ? formatMoney(p.projected_monthly_spend, currency) : null} />
              <Metric label="Policies" value={count(p.active_policies)} />
              <Metric label="Warnings" value={count(p.warning_notifications)} hint={p.unacknowledged_notifications ? `${formatInt(p.unacknowledged_notifications)} unread` : undefined} />
              <Metric label="Critical" value={count(p.critical_notifications)} />
              <Metric label="Blocked 24h" value={count(p.blocked_syncs_24h)} hint={p.pending_overrides ? `${formatInt(p.pending_overrides)} overrides pending` : undefined} />
            </MetricGrid>
          )
        }}
      </StatPanel>

      <Trends vaultId={vault_id} />
    </div>
  )
}

const Capacity = ({ stats, quota }: { stats: VaultStatsPayload; quota: number }) => {
  const c = stats.capacity ?? {}
  const extensions = Object.entries(c.top_file_extensions ?? {})
    .filter(([, size]) => typeof size === 'number' && size > 0)
    .sort((a, b) => b[1] - a[1])
  const top = extensions.slice(0, 5)
  const rest = extensions.slice(5).reduce((acc, [, size]) => acc + size, 0)
  return (
    <>
      <MetricGrid className="sm:grid-cols-4">
        <Metric label="Stored" value={bytes(c.logical_size)} hint={quota > 0 ? `quota ${formatBytes(quota)}` : 'no quota'} />
        <Metric label="On disk" value={bytes(c.physical_size)} />
        <Metric label="Cache" value={bytes(c.cache_size)} />
        <Metric label="Free" value={bytes(c.free_space)} />
        <Metric label="Files" value={count(c.file_count)} />
        <Metric label="Folders" value={count(c.directory_count)} />
        <Metric label="Average file" value={bytes(c.average_file_size)} />
        <Metric label="Largest file" value={bytes(c.largest_file_size)} />
      </MetricGrid>
      <div className="mt-5">
        <h3 className="mb-2 text-[11px] font-medium tracking-wide text-fg-subtle uppercase">Space by file type</h3>
        {top.length ? (
          <ProportionBar
            parts={[
              ...top.map(([ext, size]) => ({ label: ext ? `.${ext}` : 'no extension', value: size, display: formatBytes(size) })),
              ...(rest > 0 ? [{ label: 'other', value: rest, display: formatBytes(rest) }] : []),
            ]}
          />
        ) : (
          <p className="text-sm text-fg-faint">{c.file_count === 0 ? 'No files yet.' : NA}</p>
        )}
      </div>
    </>
  )
}

const Activity = ({ stats: a }: { stats: ActivityStats }) => {
  const rows: [string, number | null | undefined, number | null | undefined][] = [
    ['Uploads', a.uploads_24h, a.uploads_7d],
    ['Deletes', a.deletes_24h, a.deletes_7d],
    ['Moves', a.moves_24h, a.moves_7d],
    ['Renames', a.renames_24h, a.renames_7d],
    ['Copies', a.copies_24h, a.copies_7d],
    ['Restores', a.restores_24h, a.restores_7d],
  ]
  return (
    <div className="grid gap-x-8 gap-y-2 md:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]">
      <div>
        <MetricGrid>
          {rows.map(([label, day, week]) => (
            <Metric key={label} label={`${label} 24h`} value={count(day)} hint={week != null ? `${formatInt(week)} in 7 d` : undefined} />
          ))}
        </MetricGrid>
        <div className="mt-5">
          <h3 className="mb-2 text-[11px] font-medium tracking-wide text-fg-subtle uppercase">Bytes, last 24 h</h3>
          <SplitBar
            format={bytes}
            parts={[
              { label: 'Added', value: a.bytes_added_24h, tone: 'accent' },
              { label: 'Removed', value: a.bytes_removed_24h, tone: 'neutral' },
            ]}
          />
        </div>
      </div>
      <div className="min-w-0">
        <MiniList
          title="Recent activity"
          empty={a.last_activity_at ? `Last activity ${formatRelative(a.last_activity_at)}.` : 'Nothing recorded yet.'}
          items={(a.recent_activity ?? []).slice(0, 5).map((e, i) => ({
            key: `${i}-${e.path}`,
            primary: <span className="font-mono text-xs">{e.path || '/'}</span>,
            secondary: [titleCase(e.action ?? 'change'), e.user_name].filter(Boolean).join(' · '),
            meta: ago(e.occurred_at),
          }))}
        />
        {(a.top_active_users ?? []).length ? (
          <MiniList
            title="Most active"
            empty=""
            items={(a.top_active_users ?? []).slice(0, 3).map((u, i) => ({
              key: `${u.user_id ?? i}`,
              primary: u.user_name ?? (u.user_id != null ? `User #${u.user_id}` : 'Unknown'),
              meta: `${formatInt(u.count)} changes`,
            }))}
          />
        ) : null}
      </div>
    </div>
  )
}

// The series worth a chart; the rest of the snapshot set is visible in the panels above.
const TREND_KEYS = [
  'capacity_logical_size',
  'storage_vault_size_bytes',
  'activity_mutations_24h',
  'sync_bytes_24h',
  'sync_errors_24h',
  'retention_trash_bytes',
]

const WINDOWS = [
  { value: '24', label: '24 h' },
  { value: '168', label: '7 d' },
  { value: '720', label: '30 d' },
] as const

const Trends = ({ vaultId }: { vaultId: number }) => {
  const [window, setWindow] = useState<(typeof WINDOWS)[number]['value']>('168')
  const trends = useWs(
    'stats.vault.trends',
    { vault_id: vaultId, window_hours: Number(window) },
    { refetchInterval: 60_000, retry: false, placeholderData: previous => previous, select: d => d.stats as unknown as TrendsStats },
  )
  return (
    <section className="panel min-w-0 lg:col-span-2 xl:col-span-3">
      <header className="flex flex-wrap items-center justify-between gap-3 px-5 pt-4">
        <h2 className="text-[15px] font-semibold text-fg">Trends</h2>
        <Segmented label="Trend window" value={window} onChange={setWindow} options={[...WINDOWS]} />
      </header>
      <div className="px-5 pt-4 pb-5">
        {trends.isPending ? (
          <div className="grid gap-6 sm:grid-cols-2 xl:grid-cols-3">
            {TREND_KEYS.map(key => (
              <div key={key} className="skeleton h-28" />
            ))}
          </div>
        ) : trends.error || !trends.data ? (
          <p className="text-sm text-fg-subtle">Trend history is not available{trends.error ? `: ${(trends.error as Error).message}` : '.'}</p>
        ) : (
          <div className="grid gap-x-8 gap-y-6 sm:grid-cols-2 xl:grid-cols-3">
            {TREND_KEYS.map(key => {
              const series = trends.data.series?.find(s => s.key === key)
              return (
                <div key={key} className="min-w-0">
                  <h3 className="mb-2 truncate text-xs font-medium text-fg-muted">{series?.label ?? titleCase(key)}</h3>
                  <TrendChart points={series?.points ?? []} unit={series?.unit} label={series?.label ?? key} />
                </div>
              )
            })}
          </div>
        )}
      </div>
    </section>
  )
}
