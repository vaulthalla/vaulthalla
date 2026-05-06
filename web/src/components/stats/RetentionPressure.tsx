'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { CleanupStatus, RetentionStats } from '@/models/stats/retentionStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones: Record<CleanupStatus, Tone> = {
  healthy: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    bar: 'bg-emerald-300/80',
  },
  warning: {
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    bar: 'bg-amber-300/80',
  },
  overdue: {
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
    bar: 'bg-rose-300/80',
  },
  unknown: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/65',
    dot: 'bg-white/35',
    bar: 'bg-white/35',
  },
}

const cyanTone: Tone = {
  border: 'border-cyan-300/25',
  bg: 'bg-cyan-400/10',
  text: 'text-cyan-100',
  dot: 'bg-cyan-300',
  bar: 'bg-cyan-300/75',
}

function formatInt(n: number | null): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / Math.pow(1024, i)
  return `${value >= 10 ? value.toFixed(0) : value.toFixed(1)} ${units[i]}`
}

function formatDuration(seconds: number | null): string {
  if (seconds == null || !Number.isFinite(seconds) || seconds <= 0) return 'none'
  if (seconds < 60) return `${Math.floor(seconds)}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h`
  return `${Math.floor(seconds / 86400)}d`
}

function formatCheckedAt(value: number | string | null, fallback: number | null): string {
  if (typeof value === 'number' && Number.isFinite(value) && value > 0) {
    const ms = value > 1_000_000_000_000 ? value : value * 1000
    return new Date(ms).toLocaleTimeString()
  }
  if (typeof value === 'string') {
    const parsed = Date.parse(value)
    if (Number.isFinite(parsed)) return new Date(parsed).toLocaleTimeString()
  }
  return fallback ? new Date(fallback).toLocaleTimeString() : 'pending'
}

const LiveBadge = ({
  loading,
  error,
  lastUpdated,
}: {
  loading: boolean
  error: string | null
  lastUpdated: number | null
}) => (
  <div className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-3 py-1 text-xs text-white/70 backdrop-blur">
    <span
      className={[
        'h-1.5 w-1.5 rounded-full',
        error ? 'bg-rose-400/80'
        : loading ? 'bg-amber-300/80'
        : 'bg-emerald-400/80',
      ].join(' ')}
    />
    {error ? 'error' : loading ? 'updating...' : 'live'}
    {lastUpdated ? <span className="text-white/35">/</span> : null}
    {lastUpdated ? <span className="text-white/50">{new Date(lastUpdated).toLocaleTimeString()}</span> : null}
  </div>
)

const SummaryPill = ({
  label,
  value,
  detail,
  tone,
}: {
  label: string
  value: React.ReactNode
  detail?: React.ReactNode
  tone: Tone
}) => (
  <div className={['rounded-2xl border px-3 py-3 backdrop-blur', tone.border, tone.bg].join(' ')}>
    <div className="flex items-center justify-between gap-3">
      <div className="text-[11px] text-white/50 uppercase">{label}</div>
      <span className={['h-1.5 w-1.5 rounded-full', tone.dot].join(' ')} />
    </div>
    <div className={['mt-2 truncate text-lg font-semibold', tone.text].join(' ')}>{value}</div>
    {detail ? <div className="mt-1 truncate text-xs text-white/45">{detail}</div> : null}
  </div>
)

const PressureRow = ({
  label,
  count,
  total,
  detail,
  tone,
}: {
  label: string
  count: number
  total: number
  detail: React.ReactNode
  tone: Tone
}) => {
  const width = `${Math.min(100, Math.max(3, total > 0 ? (count / total) * 100 : 3))}%`
  return (
    <div className="rounded-2xl border border-white/10 bg-white/5 p-3">
      <div className="flex items-center justify-between gap-3 text-xs">
        <span className="text-white/50">{label}</span>
        <span className={['font-semibold', tone.text].join(' ')}>{formatInt(count)}</span>
      </div>
      <div className="mt-2 h-1.5 overflow-hidden rounded-full bg-white/10">
        <div className={['h-full rounded-full', tone.bar].join(' ')} style={{ width }} />
      </div>
      <div className="mt-2 truncate text-xs text-white/45">{detail}</div>
    </div>
  )
}

export function RetentionPressureBody({
  stats,
  lastUpdated,
}: {
  stats: RetentionStats
  lastUpdated: number | null
}) {
  const tone = tones[stats.cleanup_status] ?? tones.unknown
  const checkedAt = formatCheckedAt(stats.checked_at, lastUpdated)
  const pastTone = stats.cleanup_status === 'overdue' ? tones.overdue : stats.pastRetentionCount() > 0 ? tones.warning : tones.healthy
  const maxTrash = Math.max(1, stats.trashed_files_count)
  const maxSync = Math.max(1, stats.sync_events_total)
  const maxAudit = Math.max(1, stats.audit_log_entries_total)
  const maxCache = Math.max(1, stats.cache_entries_total)

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
        <SummaryPill label="Cleanup" value={stats.cleanup_status} detail={`checked ${checkedAt}`} tone={tone} />
        <SummaryPill label="Past retention" value={formatInt(stats.pastRetentionCount())} detail="trash + sync + audit + cache" tone={pastTone} />
        <SummaryPill
          label="Trash"
          value={formatBytes(stats.trashed_bytes_total)}
          detail={`${formatInt(stats.trashed_files_count)} files / oldest ${formatDuration(stats.oldest_trashed_age_seconds)}`}
          tone={stats.trashed_files_past_retention_count > 0 ? tones.warning : cyanTone}
        />
        <SummaryPill
          label="Sync events"
          value={formatInt(stats.sync_events_total)}
          detail={`${formatInt(stats.sync_events_past_retention_count)} past ${formatInt(stats.sync_event_retention_days)}d`}
          tone={stats.sync_events_past_retention_count > 0 ? tones.warning : cyanTone}
        />
        <SummaryPill
          label="Audit"
          value={formatInt(stats.audit_log_entries_total)}
          detail={`${formatInt(stats.audit_log_entries_past_retention_count)} past ${formatInt(stats.audit_log_retention_days)}d`}
          tone={stats.audit_log_entries_past_retention_count > 0 ? tones.warning : cyanTone}
        />
        <SummaryPill
          label="Cache"
          value={formatBytes(stats.cache_bytes_total)}
          detail={`${formatInt(stats.cache_eviction_candidates)} eviction candidates`}
          tone={stats.cache_eviction_candidates > 0 ? tones.warning : cyanTone}
        />
      </div>

      <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
        <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
          <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Retention backlogs</div>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            <PressureRow
              label="Trash past retention"
              count={stats.trashed_files_past_retention_count}
              total={maxTrash}
              detail={`${formatBytes(stats.trashed_bytes_past_retention)} / ${formatInt(stats.trash_retention_days)}d policy`}
              tone={stats.trashed_files_past_retention_count > 0 ? tones.warning : tones.healthy}
            />
            <PressureRow
              label="Sync events past retention"
              count={stats.sync_events_past_retention_count}
              total={maxSync}
              detail={`${formatInt(stats.sync_event_retention_days)}d / max ${formatInt(stats.sync_event_max_entries)}`}
              tone={stats.sync_events_past_retention_count > 0 ? tones.warning : tones.healthy}
            />
            <PressureRow
              label="Audit logs past retention"
              count={stats.audit_log_entries_past_retention_count}
              total={maxAudit}
              detail={`${formatInt(stats.audit_log_retention_days)}d policy / oldest ${formatDuration(stats.oldest_audit_log_age_seconds)}`}
              tone={stats.audit_log_entries_past_retention_count > 0 ? tones.warning : tones.healthy}
            />
            <PressureRow
              label="Cache expired"
              count={stats.cache_entries_expired}
              total={maxCache}
              detail={`${formatInt(stats.cache_expiry_days)}d access expiry / max ${formatBytes(stats.cache_max_size_bytes)}`}
              tone={stats.cache_entries_expired > 0 ? tones.warning : tones.healthy}
            />
          </div>
        </div>

        <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
          <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Cleanup sources</div>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            <PressureRow
              label="Share access events"
              count={stats.share_access_events_total}
              total={Math.max(1, stats.share_access_events_total)}
              detail={`oldest ${formatDuration(stats.oldest_share_access_event_age_seconds)}`}
              tone={cyanTone}
            />
            <PressureRow
              label="Cache entries"
              count={stats.cache_entries_total}
              total={maxCache}
              detail={`${formatBytes(stats.cache_bytes_total)} indexed`}
              tone={stats.cache_eviction_candidates > 0 ? tones.warning : cyanTone}
            />
          </div>
          <div className="mt-3 rounded-xl border border-white/10 bg-white/5 px-3 py-3 text-xs text-white/45">
            This command is read-only; it reports cleanup pressure from metadata tables and configured retention windows.
          </div>
        </div>
      </div>
    </div>
  )
}

export default function RetentionPressureComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.retentionStats)
  const startPolling = useStatsStore(s => s.startRetentionStatsPolling)
  const stopPolling = useStatsStore(s => s.stopRetentionStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Retention / Cleanup"
      subtitle="Trash, sync event, audit, share event, and cache cleanup pressure"
      right={right}
      className="w-full"
    >
      <div className="space-y-4">
        {wrapper.error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        ) : null}
        <RetentionPressureBody stats={wrapper.data} lastUpdated={wrapper.lastUpdated} />
      </div>
    </StatsCard>
  )
}
