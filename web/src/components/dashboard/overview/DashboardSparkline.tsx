'use client'

import type { DashboardGraphSeries } from '@/models/stats/dashboardOverview'

const DASHBOARD_GRAPH_PENDING_TEXT = 'Not enough data to render this graph yet. Perform some operations and check back in a few minutes.'

function graphSeriesColor(index: number, series: DashboardGraphSeries): string {
  if (index === 0) return '#22d3ee'
  if (series.tone === 'error') return '#fb7185'
  if (series.tone === 'warning') return '#fbbf24'
  if (series.tone === 'healthy') return '#34d399'
  const palette = ['#60a5fa', '#a78bfa', '#f472b6', '#2dd4bf', '#c084fc', '#f59e0b', '#93c5fd']
  return palette[(index - 1) % palette.length]
}

export function DashboardGraphPlaceholder() {
  return (
    <div className="flex h-full min-h-14 items-center justify-center rounded-xl border border-cyan-200/25 bg-cyan-400/10 px-3 text-center text-[11px] leading-snug text-cyan-50/75 shadow-[inset_0_0_22px_rgba(34,211,238,0.08)]">
      {DASHBOARD_GRAPH_PENDING_TEXT}
    </div>
  )
}

export function DashboardSparkline({ series, compact = false }: { series: DashboardGraphSeries[]; compact?: boolean }) {
  const activeSeries = series.filter(item => item.points.length >= 2)
  const hasMeaningfulData = activeSeries.some(item => item.points.some(point => Math.abs(point.value) > 1e-9))

  if (!activeSeries.length || !hasMeaningfulData) return <DashboardGraphPlaceholder />

  const allPoints = activeSeries.flatMap(item => item.points)
  const minTime = Math.min(...allPoints.map(point => point.created_at))
  const maxTime = Math.max(...allPoints.map(point => point.created_at))
  const minValue = Math.min(...allPoints.map(point => point.value))
  const maxValue = Math.max(...allPoints.map(point => point.value))
  const valueSpan = maxValue - minValue || 1
  const timeSpan = maxTime - minTime || 1
  const width = 240
  const height = compact ? 54 : 72
  const pad = 6
  const lineHeight = height - pad * 2

  const pointsFor = (item: DashboardGraphSeries) => item.points
    .map(point => {
      const x = pad + ((point.created_at - minTime) / timeSpan) * (width - pad * 2)
      const y = pad + (1 - ((point.value - minValue) / valueSpan)) * lineHeight
      return `${x.toFixed(1)},${y.toFixed(1)}`
    })
    .join(' ')

  return (
    <div className="flex h-full min-h-0 flex-col rounded-xl border border-white/10 bg-black/20 px-2.5 py-2">
      <svg viewBox={`0 0 ${width} ${height}`} className="min-h-14 w-full flex-1 overflow-visible" role="img" aria-label="Trend lines">
        <line x1={pad} y1={height - pad} x2={width - pad} y2={height - pad} stroke="rgba(255,255,255,0.12)" strokeWidth="1" />
        {activeSeries.map((item, index) => (
          <polyline
            key={item.key}
            points={pointsFor(item)}
            fill="none"
            stroke={graphSeriesColor(index, item)}
            strokeWidth={index === 0 ? 2.4 : 1.45}
            strokeLinecap="round"
            strokeLinejoin="round"
            opacity={index === 0 ? 0.98 : 0.72}
          />
        ))}
      </svg>
      <div className="mt-1 flex flex-wrap gap-x-2.5 gap-y-1 overflow-hidden text-[10px] leading-none">
        {activeSeries.slice(0, compact ? 3 : 5).map((item, index) => (
          <span key={item.key} className="inline-flex min-w-0 items-center gap-1 text-white/50">
            <span className="h-1.5 w-1.5 shrink-0 rounded-full" style={{ backgroundColor: graphSeriesColor(index, item) }} />
            <span className="truncate">{item.label}</span>
          </span>
        ))}
        {activeSeries.length > (compact ? 3 : 5) ?
          <span className="text-white/35">+{activeSeries.length - (compact ? 3 : 5)} lines</span>
        : null}
      </div>
    </div>
  )
}
