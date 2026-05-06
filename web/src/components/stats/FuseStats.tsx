'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { FuseOpStats as FuseOpStatsModel } from '@/models/stats/fuseStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones = {
  healthy: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    bar: 'bg-emerald-300/80',
  },
  normal: {
    border: 'border-cyan-300/25',
    bg: 'bg-cyan-400/10',
    text: 'text-cyan-100',
    dot: 'bg-cyan-300',
    bar: 'bg-cyan-300/75',
  },
  warn: {
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
} satisfies Record<string, Tone>

function toneForErrorRate(rate: number): Tone {
  if (!Number.isFinite(rate)) return tones.unknown
  if (rate === 0) return tones.healthy
  if (rate <= 0.02) return tones.normal
  if (rate <= 0.1) return tones.warn
  return tones.critical
}

function formatInt(n: number): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function formatPercent(n: number): string {
  return `${((Number.isFinite(n) ? Math.max(0, n) : 0) * 100).toFixed(2)}%`
}

function formatMs(n: number): string {
  return `${(Number.isFinite(n) ? Math.max(0, n) : 0).toFixed(2)} ms`
}

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / Math.pow(1024, i)
  return `${value >= 10 ? value.toFixed(0) : value.toFixed(1)} ${units[i]}`
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
      <span className="text-white/35">/</span>
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
    <div className={['mt-2 truncate text-lg font-semibold', tone.text].join(' ')}>{value}</div>
    {detail ?
      <div className="mt-1 truncate text-xs text-white/45">{detail}</div>
    : null}
  </div>
)

const OpRow = ({ op }: { op: FuseOpStatsModel }) => {
  const tone = toneForErrorRate(op.error_rate)
  const barWidth = `${Math.min(100, Math.max(3, op.count > 0 ? op.error_rate * 100 : 3))}%`

  return (
    <div className="grid grid-cols-2 gap-2 rounded-2xl border border-white/10 bg-white/5 p-3 text-sm backdrop-blur md:grid-cols-9">
      <div className="col-span-2 min-w-0 md:col-span-1">
        <div className="truncate font-semibold text-white/90">{op.op}</div>
        <div className={['mt-1 h-1.5 overflow-hidden rounded-full bg-white/10'].join(' ')}>
          <div className={['h-full rounded-full', tone.bar].join(' ')} style={{ width: barWidth }} />
        </div>
      </div>
      <Metric label="count" value={formatInt(op.count)} />
      <Metric label="success" value={formatInt(op.successes)} />
      <Metric label="errors" value={formatInt(op.errors)} tone={op.errors > 0 ? tone : undefined} />
      <Metric label="err rate" value={formatPercent(op.error_rate)} tone={tone} />
      <Metric label="avg" value={formatMs(op.avg_ms)} />
      <Metric label="max" value={formatMs(op.max_ms)} tone={op.max_ms > 100 ? tones.warn : undefined} />
      <Metric label="read" value={formatBytes(op.bytes_read)} />
      <Metric label="write" value={formatBytes(op.bytes_written)} />
    </div>
  )
}

const Metric = ({ label, value, tone }: { label: string; value: React.ReactNode; tone?: Tone }) => (
  <div className="min-w-0">
    <div className="text-[11px] text-white/40 uppercase">{label}</div>
    <div className={['mt-1 truncate text-xs font-semibold sm:text-sm', tone?.text ?? 'text-white/80'].join(' ')}>
      {value}
    </div>
  </div>
)

export default function FuseStatsComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.fuseStats)
  const startPolling = useStatsStore(s => s.startFuseStatsPolling)
  const stopPolling = useStatsStore(s => s.stopFuseStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const stats = wrapper.data
  const tone = toneForErrorRate(stats.error_rate)
  const slowest = stats.total_ops > 0 ? stats.topOpsByLatency(1)[0] : null
  const checkedAt = formatCheckedAt(stats.checked_at, wrapper.lastUpdated)
  const visibleOps = [...stats.ops].sort((a, b) => {
    if (b.count !== a.count) return b.count - a.count
    return b.max_ms - a.max_ms
  })

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="FUSE Operations"
      subtitle="Live filesystem operation traffic, errors, latency, and handles"
      right={right}
      className="w-full">
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Ops" value={formatInt(stats.total_ops)} detail={`checked ${checkedAt}`} tone={tone} />
          <SummaryPill
            label="Error rate"
            value={formatPercent(stats.error_rate)}
            detail={`${formatInt(stats.total_errors)} errors`}
            tone={tone}
          />
          <SummaryPill label="Read" value={formatBytes(stats.read_bytes)} detail="returned bytes" tone={tones.normal} />
          <SummaryPill
            label="Write"
            value={formatBytes(stats.write_bytes)}
            detail="accepted bytes"
            tone={tones.normal}
          />
          <SummaryPill
            label="Handles"
            value={`${formatInt(stats.open_handles_current)} / ${formatInt(stats.open_handles_peak)}`}
            detail="current / peak"
            tone={stats.open_handles_current > 0 ? tones.healthy : tones.unknown}
          />
          <SummaryPill
            label="Slowest"
            value={slowest ? slowest.op : 'none'}
            detail={slowest ? formatMs(slowest.max_ms) : 'no samples'}
            tone={slowest && slowest.max_ms > 100 ? tones.warn : tones.unknown}
          />
        </div>

        {wrapper.error ?
          <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs text-rose-200/90">
            {wrapper.error}
          </div>
        : null}

        <div className="space-y-2">
          <div className="flex items-center justify-between gap-3">
            <div className="text-xs font-semibold text-white/70 uppercase">Operations</div>
            <div className="text-xs text-white/40">{formatBytes(stats.totalBytes())} total traffic</div>
          </div>
          <div className="space-y-2">
            {visibleOps.map(op => (
              <OpRow key={op.op} op={op} />
            ))}
            {visibleOps.length === 0 ?
              <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-4 text-sm text-white/55">
                No FUSE operation snapshot reported.
              </div>
            : null}
          </div>
        </div>

        <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
          <div className="mb-3 flex items-center justify-between gap-3">
            <div className="text-xs font-semibold text-white/70 uppercase">Top errno values</div>
            <div className="text-xs text-white/40">{formatInt(stats.top_errors.length)} reported</div>
          </div>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 lg:grid-cols-5">
            {stats.top_errors.map(err => (
              <div
                key={`${err.errno_value}:${err.name}`}
                className="rounded-2xl border border-white/10 bg-black/10 px-3 py-2">
                <div className="truncate text-sm font-semibold text-rose-100">{err.name}</div>
                <div className="mt-1 text-xs text-white/45">
                  errno {err.errno_value} / {formatInt(err.count)}
                </div>
              </div>
            ))}
            {stats.top_errors.length === 0 ?
              <div className="rounded-2xl border border-emerald-300/20 bg-emerald-400/10 px-3 py-2 text-sm text-emerald-100">
                No errors recorded.
              </div>
            : null}
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
