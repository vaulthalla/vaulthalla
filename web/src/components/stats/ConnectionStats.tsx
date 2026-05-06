'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { ConnectionHealthStatus } from '@/models/stats/connectionStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones: Record<ConnectionHealthStatus, Tone> = {
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

const ModeRow = ({ label, value, max, tone }: { label: string; value: number; max: number; tone: Tone }) => {
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

export default function ConnectionStatsComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.connectionStats)
  const startPolling = useStatsStore(s => s.startConnectionStatsPolling)
  const stopPolling = useStatsStore(s => s.stopConnectionStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const stats = wrapper.data
  const tone = tones[stats.status] ?? tones.unknown
  const unauthTone = stats.active_unauthenticated_sessions > 0 ? tones.warning : tones.healthy
  const checkedAt = formatCheckedAt(stats.checked_at, wrapper.lastUpdated)
  const maxModeCount = Math.max(1, ...Object.values(stats.sessions_by_mode))
  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Connection Health"
      subtitle="Active websocket sessions split by human, share, pending share, and unauthenticated modes"
      right={right}
      className="w-full"
    >
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Status" value={stats.status} detail={`checked ${checkedAt}`} tone={tone} />
          <SummaryPill label="Sessions" value={formatInt(stats.active_ws_sessions_total)} detail="active websocket" tone={cyanTone} />
          <SummaryPill label="Human" value={formatInt(stats.active_human_sessions)} detail="authenticated users" tone={cyanTone} />
          <SummaryPill label="Share" value={formatInt(stats.active_share_sessions)} detail={`${formatInt(stats.active_share_pending_sessions)} pending`} tone={cyanTone} />
          <SummaryPill
            label="Unauth"
            value={formatInt(stats.active_unauthenticated_sessions)}
            detail={`oldest ${formatDuration(stats.oldest_unauthenticated_session_age_seconds)}`}
            tone={unauthTone}
          />
          <SummaryPill label="Oldest" value={formatDuration(stats.oldest_session_age_seconds)} detail="session age" tone={cyanTone} />
        </div>

        {wrapper.error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        ) : null}

        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Sessions by mode</div>
            <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
              <ModeRow label="Human" value={stats.sessions_by_mode.human ?? 0} max={maxModeCount} tone={cyanTone} />
              <ModeRow label="Share" value={stats.sessions_by_mode.share ?? 0} max={maxModeCount} tone={cyanTone} />
              <ModeRow label="Share pending" value={stats.sessions_by_mode.share_pending ?? 0} max={maxModeCount} tone={tones.warning} />
              <ModeRow label="Unauthenticated" value={stats.sessions_by_mode.unauthenticated ?? 0} max={maxModeCount} tone={unauthTone} />
            </div>
          </div>

          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Lifecycle settings</div>
            <div className="grid grid-cols-1 gap-2 sm:grid-cols-3">
              <SummaryPill label="Idle timeout" value={`${formatInt(stats.idle_timeout_minutes)}m`} detail="configured" tone={cyanTone} />
              <SummaryPill label="Unauth timeout" value={formatDuration(stats.unauthenticated_timeout_seconds)} detail="configured" tone={cyanTone} />
              <SummaryPill label="Sweep" value={formatDuration(stats.sweep_interval_seconds)} detail="interval" tone={cyanTone} />
            </div>
            <div className="mt-3 rounded-xl border border-white/10 bg-white/5 px-3 py-3 text-xs text-white/45">
              Recent open/close/error counters are not available yet; this card reports the live active session registry and configured timeout policy.
            </div>
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
