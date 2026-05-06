'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { OperationQueueStatus, OperationStats } from '@/models/stats/operationStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones: Record<OperationQueueStatus, Tone> = {
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
  critical: {
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

const CountBar = ({ label, value, max, tone }: { label: string; value: number; max: number; tone: Tone }) => {
  const width = `${Math.min(100, Math.max(3, max > 0 ? (value / max) * 100 : 3))}%`
  return (
    <div className="rounded-2xl border border-white/10 bg-white/5 p-3">
      <div className="flex items-center justify-between gap-3 text-xs">
        <span className="text-white/50">{label}</span>
        <span className={['font-semibold', tone.text].join(' ')}>{formatInt(value)}</span>
      </div>
      <div className="mt-2 h-1.5 overflow-hidden rounded-full bg-white/10">
        <div className={['h-full rounded-full', tone.bar].join(' ')} style={{ width }} />
      </div>
    </div>
  )
}

export function OperationQueueBody({
  stats,
  lastUpdated,
}: {
  stats: OperationStats
  lastUpdated: number | null
}) {
  const tone = tones[stats.overall_status] ?? tones.unknown
  const stuckTone = stats.stuckWorkCount() > 0 ? tones.critical : tones.healthy
  const failedTone = stats.failedWork24h() > 0 || stats.cancelled_operations_24h > 0 ? tones.warning : tones.healthy
  const uploadProgress =
    stats.upload_bytes_expected_active > 0 ?
      Math.min(1, stats.upload_bytes_received_active / stats.upload_bytes_expected_active)
    : 0
  const checkedAt = formatCheckedAt(stats.checked_at, lastUpdated)
  const maxTypeCount = Math.max(1, ...Object.values(stats.operations_by_type))
  const maxStatusCount = Math.max(1, ...Object.values(stats.operations_by_status))

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
        <SummaryPill label="Status" value={stats.overall_status} detail={`checked ${checkedAt}`} tone={tone} />
        <SummaryPill
          label="Pending"
          value={formatInt(stats.pending_operations)}
          detail={`oldest ${formatDuration(stats.oldest_pending_operation_age_seconds)}`}
          tone={stats.pending_operations > 0 ? tones.warning : tones.healthy}
        />
        <SummaryPill
          label="In progress"
          value={formatInt(stats.in_progress_operations)}
          detail={`oldest ${formatDuration(stats.oldest_in_progress_operation_age_seconds)}`}
          tone={stats.in_progress_operations > 0 ? cyanTone : tones.healthy}
        />
        <SummaryPill label="Stalled" value={formatInt(stats.stuckWorkCount())} detail="ops + uploads" tone={stuckTone} />
        <SummaryPill label="Failed 24h" value={formatInt(stats.failedWork24h())} detail="ops + uploads" tone={failedTone} />
        <SummaryPill
          label="Active uploads"
          value={formatInt(stats.active_share_uploads)}
          detail={`${formatBytes(stats.upload_bytes_received_active)} / ${formatBytes(stats.upload_bytes_expected_active)}`}
          tone={stats.active_share_uploads > 0 ? cyanTone : tones.healthy}
        />
      </div>

      <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
        <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
          <div className="mb-3 flex items-center justify-between gap-3">
            <div className="text-xs font-semibold text-white/75 uppercase">Filesystem operations</div>
            <div className="text-xs text-white/40">stale after {formatDuration(stats.stale_threshold_seconds)}</div>
          </div>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-3">
            <CountBar label="Move" value={stats.operations_by_type.move ?? 0} max={maxTypeCount} tone={cyanTone} />
            <CountBar label="Copy" value={stats.operations_by_type.copy ?? 0} max={maxTypeCount} tone={cyanTone} />
            <CountBar label="Rename" value={stats.operations_by_type.rename ?? 0} max={maxTypeCount} tone={cyanTone} />
          </div>
          <div className="mt-2 grid grid-cols-1 gap-2 sm:grid-cols-5">
            <CountBar label="Pending" value={stats.operations_by_status.pending ?? 0} max={maxStatusCount} tone={tones.warning} />
            <CountBar label="Running" value={stats.operations_by_status.in_progress ?? 0} max={maxStatusCount} tone={cyanTone} />
            <CountBar label="Success" value={stats.operations_by_status.success ?? 0} max={maxStatusCount} tone={tones.healthy} />
            <CountBar label="Error" value={stats.operations_by_status.error ?? 0} max={maxStatusCount} tone={tones.critical} />
            <CountBar label="Cancelled" value={stats.operations_by_status.cancelled ?? 0} max={maxStatusCount} tone={tones.warning} />
          </div>
        </div>

        <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
          <div className="mb-3 flex items-center justify-between gap-3">
            <div className="text-xs font-semibold text-white/75 uppercase">Share uploads</div>
            <div className="text-xs text-white/40">oldest active {formatDuration(stats.oldest_active_upload_age_seconds)}</div>
          </div>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-3">
            <CountBar label="Active" value={stats.active_share_uploads} max={Math.max(1, stats.active_share_uploads)} tone={cyanTone} />
            <CountBar label="Stalled" value={stats.stalled_share_uploads} max={Math.max(1, stats.stalled_share_uploads)} tone={stuckTone} />
            <CountBar label="Failed 24h" value={stats.failed_share_uploads_24h} max={Math.max(1, stats.failed_share_uploads_24h)} tone={failedTone} />
          </div>
          <div className="mt-3 h-2 overflow-hidden rounded-full bg-white/10">
            <div className="h-full rounded-full bg-cyan-300/75" style={{ width: `${Math.max(3, uploadProgress * 100)}%` }} />
          </div>
          <div className="mt-2 text-xs text-white/45">
            Active upload bytes: {formatBytes(stats.upload_bytes_received_active)} received of {formatBytes(stats.upload_bytes_expected_active)}
          </div>
        </div>
      </div>

      <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
        <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Recent errors</div>
        {stats.recent_operation_errors.length ? (
          <div className="space-y-2">
            {stats.recent_operation_errors.map((err, idx) => (
              <div key={`${err.source}-${err.path}-${idx}`} className="grid grid-cols-1 gap-2 rounded-xl border border-white/10 bg-white/5 p-3 text-xs md:grid-cols-[130px_100px_1fr_1fr]">
                <div className="font-semibold text-white/75">{err.source}</div>
                <div className="text-white/55">{err.operation} / {err.status}</div>
                <div className="min-w-0 truncate text-white/55">{err.path || 'unknown path'}</div>
                <div className="min-w-0 truncate text-rose-100/80">{err.error ?? 'no error text'}</div>
              </div>
            ))}
          </div>
        ) : (
          <div className="rounded-xl border border-white/10 bg-white/5 px-3 py-3 text-sm text-white/50">
            No recent operation or upload errors recorded.
          </div>
        )}
      </div>
    </div>
  )
}

export default function OperationQueueStatsComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.operationStats)
  const startPolling = useStatsStore(s => s.startOperationStatsPolling)
  const stopPolling = useStatsStore(s => s.stopOperationStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Operation Queue"
      subtitle="Global filesystem operation and share-upload pressure"
      right={right}
      className="w-full"
    >
      <OperationQueueBody stats={wrapper.data} lastUpdated={wrapper.lastUpdated} />
    </StatsCard>
  )
}
