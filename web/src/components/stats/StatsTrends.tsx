'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { StatsTrendSeries, StatsTrends, StatsTrendUnit } from '@/models/stats/statsTrends'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; line: string; fill: string }

const tones: Tone[] = [
  {
    border: 'border-cyan-300/25',
    bg: 'bg-cyan-400/10',
    text: 'text-cyan-100',
    dot: 'bg-cyan-300',
    line: 'stroke-cyan-300',
    fill: 'fill-cyan-300',
  },
  {
    border: 'border-emerald-300/25',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    line: 'stroke-emerald-300',
    fill: 'fill-emerald-300',
  },
  {
    border: 'border-amber-300/25',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    line: 'stroke-amber-300',
    fill: 'fill-amber-300',
  },
  {
    border: 'border-sky-300/25',
    bg: 'bg-sky-400/10',
    text: 'text-sky-100',
    dot: 'bg-sky-300',
    line: 'stroke-sky-300',
    fill: 'fill-sky-300',
  },
]

const emptyTone: Tone = {
  border: 'border-white/10',
  bg: 'bg-white/5',
  text: 'text-white/65',
  dot: 'bg-white/35',
  line: 'stroke-white/35',
  fill: 'fill-white/35',
}

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / Math.pow(1024, i)
  return `${value >= 10 ? value.toFixed(0) : value.toFixed(1)} ${units[i]}`
}

function formatCount(value: number): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(value || 0)))
}

function formatRatio(value: number): string {
  if (!Number.isFinite(value)) return '0%'
  return `${(value * 100).toFixed(value < 0.1 ? 2 : 1)}%`
}

function formatValue(value: number, unit: StatsTrendUnit): string {
  if (unit === 'bytes') return formatBytes(value)
  if (unit === 'ratio') return formatRatio(value)
  return formatCount(value)
}

function formatDelta(value: number | null, unit: StatsTrendUnit): string {
  if (value == null || !Number.isFinite(value)) return 'none'
  const prefix = value > 0 ? '+' : ''
  if (unit === 'bytes') return `${prefix}${formatBytes(value)}`
  if (unit === 'ratio') return `${prefix}${(value * 100).toFixed(2)}pp`
  return `${prefix}${formatCount(value)}`
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

function Sparkline({ series, tone }: { series: StatsTrendSeries; tone: Tone }) {
  const points = series.points
  const width = 280
  const height = 64

  if (points.length === 0) {
    return <div className="h-16 rounded-xl border border-white/10 bg-black/20" />
  }

  const values = points.map(point => point.value)
  const min = Math.min(...values)
  const max = Math.max(...values)
  const span = max - min || 1
  const step = points.length > 1 ? width / (points.length - 1) : width
  const polyline = points
    .map((point, index) => {
      const x = points.length > 1 ? index * step : width / 2
      const y = height - ((point.value - min) / span) * (height - 10) - 5
      return `${x.toFixed(2)},${y.toFixed(2)}`
    })
    .join(' ')

  return (
    <svg className="h-16 w-full overflow-visible" viewBox={`0 0 ${width} ${height}`} role="img" aria-label={series.label}>
      <line x1="0" y1={height - 5} x2={width} y2={height - 5} className="stroke-white/10" strokeWidth="1" />
      <polyline points={polyline} fill="none" className={tone.line} strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round" />
      {points.length === 1 ? <circle cx={width / 2} cy={height / 2} r="3" className={tone.fill} /> : null}
    </svg>
  )
}

function TrendSeriesCard({ series, tone }: { series: StatsTrendSeries; tone: Tone }) {
  const latest = series.latest()
  const latestValue = latest ? formatValue(latest.value, series.unit) : 'no data'
  const delta24h = series.deltaSince(24)
  const delta7d = series.deltaSince(168)

  return (
    <div className={['rounded-2xl border bg-black/10 p-3', tone.border].join(' ')}>
      <div className="flex items-start justify-between gap-3">
        <div className="min-w-0">
          <div className="truncate text-sm font-semibold text-white/85">{series.label}</div>
          <div className="mt-1 truncate text-xs text-white/40">{series.snapshot_type}</div>
        </div>
        <div className={['shrink-0 rounded-full px-2 py-1 text-xs font-semibold', tone.bg, tone.text].join(' ')}>
          {latestValue}
        </div>
      </div>
      <div className="mt-3">
        <Sparkline series={series} tone={tone} />
      </div>
      <div className="mt-3 grid grid-cols-2 gap-2 text-xs">
        <div className="rounded-xl border border-white/10 bg-white/5 px-2 py-2">
          <div className="text-white/40">24h</div>
          <div className="mt-1 font-semibold text-white/75">{formatDelta(delta24h, series.unit)}</div>
        </div>
        <div className="rounded-xl border border-white/10 bg-white/5 px-2 py-2">
          <div className="text-white/40">7d</div>
          <div className="mt-1 font-semibold text-white/75">{formatDelta(delta7d, series.unit)}</div>
        </div>
      </div>
    </div>
  )
}

export function StatsTrendsBody({ stats, lastUpdated }: { stats: StatsTrends; lastUpdated: number | null }) {
  const checkedAt = formatCheckedAt(stats.checked_at, lastUpdated)
  const hasData = stats.hasData()
  const latestPointCount = stats.series.reduce((sum, series) => sum + series.points.length, 0)

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-4">
        <SummaryPill label="Window" value={`${formatCount(stats.window_hours)}h`} detail="trend range" tone={hasData ? tones[0] : emptyTone} />
        <SummaryPill label="Series" value={formatCount(stats.series.length)} detail={`${formatCount(latestPointCount)} snapshots`} tone={hasData ? tones[1] : emptyTone} />
        <SummaryPill label="Scope" value={stats.scope} detail={stats.vault_id ? `vault ${stats.vault_id}` : 'system'} tone={hasData ? tones[3] : emptyTone} />
        <SummaryPill label="Checked" value={checkedAt} detail="latest query" tone={hasData ? tones[0] : emptyTone} />
      </div>

      {hasData ? (
        <div className="grid grid-cols-1 gap-3 lg:grid-cols-2">
          {stats.series.map((series, index) => (
            <TrendSeriesCard key={series.key} series={series} tone={tones[index % tones.length]} />
          ))}
        </div>
      ) : (
        <div className="rounded-2xl border border-white/10 bg-white/5 px-4 py-5 text-sm text-white/55">
          No historical trend snapshots have been collected yet.
        </div>
      )}
    </div>
  )
}

export default function StatsTrendsComponent({ intervalMs = 30000 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.systemTrends)
  const startPolling = useStatsStore(s => s.startSystemTrendsPolling)
  const stopPolling = useStatsStore(s => s.stopSystemTrendsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Trends"
      subtitle="24h and 7d lines from persisted dashboard snapshots"
      right={right}
      className="w-full"
    >
      <div className="space-y-4">
        {wrapper.error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        ) : null}
        <StatsTrendsBody stats={wrapper.data} lastUpdated={wrapper.lastUpdated} />
      </div>
    </StatsCard>
  )
}
