'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type {
  ThreadPoolManagerStatus,
  ThreadPoolStats as ThreadPoolStatsModel,
  ThreadPoolStatus,
} from '@/models/stats/threadPoolStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; soft: string; bar: string }

const statusTones: Record<ThreadPoolStatus | ThreadPoolManagerStatus, Tone> = {
  healthy: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    soft: 'text-emerald-200/80',
    bar: 'bg-emerald-300/80',
  },
  idle: {
    border: 'border-cyan-300/25',
    bg: 'bg-cyan-400/10',
    text: 'text-cyan-100',
    dot: 'bg-cyan-300',
    soft: 'text-cyan-200/80',
    bar: 'bg-cyan-300/75',
  },
  normal: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/85',
    dot: 'bg-cyan-200/80',
    soft: 'text-white/60',
    bar: 'bg-cyan-200/65',
  },
  pressured: {
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    soft: 'text-amber-200/80',
    bar: 'bg-amber-300/80',
  },
  saturated: {
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
    soft: 'text-rose-200/80',
    bar: 'bg-rose-300/80',
  },
  degraded: {
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
    soft: 'text-rose-200/80',
    bar: 'bg-rose-300/80',
  },
  unknown: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/65',
    dot: 'bg-white/35',
    soft: 'text-white/55',
    bar: 'bg-white/35',
  },
}

function formatInt(n: number): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function formatPressure(n: number): string {
  return `${(Number.isFinite(n) ? Math.max(0, n) : 0).toFixed(2)}x`
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
    {error ?
      'error'
    : loading ?
      'updating...'
    : 'live'}
    {lastUpdated ?
      <span className="text-white/35">·</span>
    : null}
    {lastUpdated ?
      <span className="text-white/50">{new Date(lastUpdated).toLocaleTimeString()}</span>
    : null}
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
    <div className={['mt-2 text-lg font-semibold', tone.text].join(' ')}>{value}</div>
    {detail ?
      <div className="mt-1 text-xs text-white/45">{detail}</div>
    : null}
  </div>
)

const PoolCard = ({ pool }: { pool: ThreadPoolStatsModel }) => {
  const tone = statusTones[pool.status] ?? statusTones.unknown
  const pressureWidth = `${Math.min(100, Math.max(3, (pool.pressure_ratio / 8) * 100))}%`

  return (
    <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
      <div className="flex items-start justify-between gap-3">
        <div className="min-w-0">
          <div className="truncate text-sm font-semibold text-white/90">{pool.name.toUpperCase()}</div>
          <div className="mt-1 text-xs text-white/45">
            idle {formatInt(pool.idle_worker_count)} / busy {formatInt(pool.busy_worker_count)}
          </div>
        </div>
        <div
          className={['shrink-0 rounded-full border px-2.5 py-1 text-xs', tone.border, tone.bg, tone.text].join(' ')}>
          {pool.status}
        </div>
      </div>

      <div className="mt-4 h-2 overflow-hidden rounded-full bg-white/10">
        <div className={['h-full rounded-full', tone.bar].join(' ')} style={{ width: pressureWidth }} />
      </div>

      <div className="mt-3 grid grid-cols-2 gap-2">
        <Metric label="workers" value={formatInt(pool.worker_count)} />
        <Metric label="queue" value={formatInt(pool.queue_depth)} />
        <Metric label="pressure" value={formatPressure(pool.pressure_ratio)} />
        <Metric label="borrowed" value={formatInt(pool.borrowed_worker_count)} />
      </div>
    </div>
  )
}

const Metric = ({ label, value }: { label: string; value: React.ReactNode }) => (
  <div className="rounded-2xl border border-white/10 bg-black/10 px-3 py-2">
    <div className="text-[11px] text-white/45 uppercase">{label}</div>
    <div className="mt-1 text-sm font-semibold text-white/85">{value}</div>
  </div>
)

export default function ThreadPoolStatsComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.threadPoolStats)
  const startPolling = useStatsStore(s => s.startThreadPoolStatsPolling)
  const stopPolling = useStatsStore(s => s.stopThreadPoolStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const stats = wrapper.data
  const tone = statusTones[stats.overall_status] ?? statusTones.unknown
  const checkedAt = formatCheckedAt(stats.checked_at, wrapper.lastUpdated)

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Thread Pools"
      subtitle="Runtime worker pressure across FUSE, sync, thumbnails, HTTP, and stats"
      right={right}
      className="w-full">
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Overall" value={stats.overall_status} detail={`checked ${checkedAt}`} tone={tone} />
          <SummaryPill
            label="Workers"
            value={formatInt(stats.total_worker_count)}
            detail="allocated"
            tone={statusTones.normal}
          />
          <SummaryPill label="Queue" value={formatInt(stats.total_queue_depth)} detail="pending tasks" tone={tone} />
          <SummaryPill
            label="Idle"
            value={formatInt(stats.total_idle_worker_count)}
            detail="available"
            tone={stats.total_idle_worker_count > 0 ? statusTones.idle : statusTones.pressured}
          />
          <SummaryPill
            label="Borrowed"
            value={formatInt(stats.total_borrowed_worker_count)}
            detail="rebalanced"
            tone={stats.total_borrowed_worker_count > 0 ? statusTones.pressured : statusTones.normal}
          />
          <SummaryPill
            label="Pressure"
            value={formatPressure(stats.max_pressure_ratio)}
            detail={`${formatInt(stats.pressured_pool_count)} pressured · ${formatInt(stats.saturated_pool_count)} saturated`}
            tone={tone}
          />
        </div>

        {wrapper.error ?
          <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs text-rose-200/90">
            {wrapper.error}
          </div>
        : null}

        <div className="grid grid-cols-1 gap-3 md:grid-cols-2 xl:grid-cols-5">
          {stats.pools.map(pool => (
            <PoolCard key={pool.name} pool={pool} />
          ))}
          {stats.pools.length === 0 ?
            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-4 text-sm text-white/55">
              No thread pool snapshot reported.
            </div>
          : null}
        </div>
      </div>
    </StatsCard>
  )
}
