'use client'

import Link from 'next/link'
import { useRouter } from 'next/navigation'
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react'

import {
  DASHBOARD_LAYOUT_STORAGE_KEY,
  dashboardCardSizes,
  dashboardCardVariants,
  dashboardLayoutInstanceId,
  dashboardLayoutPreference,
  type DashboardCardSize,
  type DashboardCardVariant,
  type DashboardLayoutCard,
  normalizeDashboardLayout,
  visibleDashboardLayoutCards,
} from '@/models/dashboard/dashboardLayout'
import { DASHBOARD_HOME_PREFERENCE_KEY } from '@/models/dashboard/dashboardPreferences'
import {
  DashboardCardSummary,
  type DashboardGraphSeries,
  type DashboardMetricSummary,
  type DashboardOverviewRequest,
} from '@/models/stats/dashboardOverview'
import {
  dashboardCardCatalog,
  dashboardCardCatalogById,
  dashboardLayoutFromCards,
  dashboardLayoutPresets,
  defaultDashboardLayout,
  type DashboardCardCatalogItem,
  type DashboardLayoutPreset,
} from '@/components/dashboard/dashboardCardCatalog'
import { DashboardIssueList, DashboardIssueSummaryLine, dedupeDashboardIssues } from '@/components/dashboard/DashboardIssueList'
import { DashboardSeverityBadge, DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardSeverityTone, sortDashboardIssues } from '@/components/dashboard/dashboardSeverity'
import {
  dashboardMetricByKey,
  dashboardMetricMeterValue,
  dashboardMetricNumber,
  dashboardGraphMetricKeys,
  dashboardVisualMetricKeys,
  selectDashboardCardMetrics,
} from '@/components/dashboard/dashboardMetricCuration'
import GridPlusIcon from '@/fa-duotone/grid-2-plus.svg'
import { useDashboardPreferencesStore } from '@/stores/dashboardPreferencesStore'
import { useStatsStore } from '@/stores/statsStore'

function formatCheckedAt(value: number | string | null, fallback: number | null): string {
  if (typeof value === 'number' && Number.isFinite(value) && value > 0) {
    const ms = value > 1_000_000_000_000 ? value : value * 1000
    return new Date(ms).toLocaleTimeString()
  }

  if (typeof value === 'string' && value.length > 0) {
    const parsed = Date.parse(value)
    if (Number.isFinite(parsed)) return new Date(parsed).toLocaleTimeString()
  }

  return fallback ? new Date(fallback).toLocaleTimeString() : 'pending'
}

function makeDefaultLayout(): DashboardLayoutCard[] {
  const defaults = defaultDashboardLayout()
  return normalizeDashboardLayout(defaults, dashboardCardCatalog, defaults)
}

function loadStoredLayout(): DashboardLayoutCard[] {
  const defaults = makeDefaultLayout()
  if (typeof window === 'undefined') return defaults

  try {
    const raw = window.localStorage.getItem(DASHBOARD_LAYOUT_STORAGE_KEY)
    if (!raw) return defaults
    return normalizeDashboardLayout(JSON.parse(raw), dashboardCardCatalog, defaults)
  } catch {
    return defaults
  }
}

function storeLayout(layout: DashboardLayoutCard[]) {
  if (typeof window === 'undefined') return
  window.localStorage.setItem(DASHBOARD_LAYOUT_STORAGE_KEY, JSON.stringify(dashboardLayoutPreference(layout)))
}

function layoutKey(layout: DashboardLayoutCard[]): string {
  return JSON.stringify(dashboardLayoutPreference(layout).cards)
}

const SYSTEM_HEALTH_CARD_ID = 'system.health'

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

function CommandHealthMetric({ metric }: { metric: DashboardMetricSummary }) {
  const tone = dashboardSeverityTone(metric.tone)

  return (
    <Link
      href={metric.href || '/dashboard/runtime#system-health'}
      className={[
        'inline-flex items-center gap-2 rounded-full border bg-black/20 px-2.5 py-1 text-xs transition hover:brightness-125',
        tone.border,
        tone.bg,
      ].join(' ')}>
      <span className="text-white/45">{metric.label}</span>
      <span className={['font-semibold', tone.text].join(' ')}>{metric.value || 'unknown'}</span>
    </Link>
  )
}

function DashboardMetricTile({ metric, dense = false }: { metric: DashboardMetricSummary; dense?: boolean }) {
  const tone = dashboardSeverityTone(metric.tone)
  const body = (
    <div
      className={[
        'min-w-0 rounded-lg border',
        dense ? 'px-2 py-1' : 'px-2.5 py-1.5',
        tone.border,
        tone.bg,
      ].join(' ')}>
      <div className="truncate text-[8.5px] leading-none tracking-[0.06em] text-white/45 uppercase">{metric.label}</div>
      <div
        className={[
          'mt-1 truncate font-semibold leading-none',
          dense ? 'text-[14px]' : 'text-[16px]',
          tone.text,
        ].join(' ')}>
        {metric.value || 'unknown'}
      </div>
    </div>
  )

  return metric.href ?
      <Link href={metric.href} className="transition hover:brightness-125">
        {body}
      </Link>
    : body
}

function ratioValue(value: string): number | null {
  const [ready, total] = value.split('/').map(part => Number(part.trim()))
  if (!Number.isFinite(ready) || !Number.isFinite(total) || total <= 0) return null
  return Math.max(0, Math.min(1, ready / total))
}

function VisualMeter({ metric }: { metric: DashboardMetricSummary }) {
  const tone = dashboardSeverityTone(metric.tone)
  const meter = dashboardMetricMeterValue(metric) ?? ratioValue(metric.value)
  if (meter === null) return null

  return (
    <div className={['rounded-xl border px-2.5 py-2', tone.border, tone.bg].join(' ')}>
      <div className="flex items-center justify-between gap-3 text-[11px]">
        <span className="text-white/55">{metric.label}</span>
        <span className={['font-semibold', tone.text].join(' ')}>{metric.value}</span>
      </div>
      <div className="mt-1.5 h-1.5 overflow-hidden rounded-full bg-white/10">
        <div className={['h-full rounded-full', tone.dot].join(' ')} style={{ width: `${Math.max(4, meter * 100)}%` }} />
      </div>
    </div>
  )
}

function VisualStack({
  segments,
  emptyLabel,
}: {
  segments: Array<{ key: string; label: string; value: number; tone: ReturnType<typeof dashboardSeverityTone> }>
  emptyLabel: string
}) {
  const total = segments.reduce((sum, segment) => sum + Math.max(0, segment.value), 0)

  return (
    <div className="rounded-xl border border-white/10 bg-black/20 px-2.5 py-2">
      <div className="h-1.5 overflow-hidden rounded-full bg-white/10">
        {total > 0 ?
          <div className="flex h-full w-full">
            {segments.filter(segment => segment.value > 0).map(segment => (
              <div
                key={segment.key}
                className={segment.tone.dot}
                style={{ width: `${Math.max(5, (segment.value / total) * 100)}%` }}
                title={`${segment.label}: ${segment.value}`}
              />
            ))}
          </div>
        : <div className="h-full w-full rounded-full bg-emerald-300/60" />}
      </div>
      <div className="mt-1.5 flex flex-wrap gap-x-2.5 gap-y-1 text-[10.5px]">
        {total > 0 ?
          segments.map(segment => (
            <span key={segment.key} className="inline-flex items-center gap-1 text-white/55">
              <span className={['h-1.5 w-1.5 rounded-full', segment.tone.dot].join(' ')} />
              {segment.label} <span className="text-white/80">{segment.value}</span>
            </span>
          ))
        : <span className="text-emerald-100/80">{emptyLabel}</span>}
      </div>
    </div>
  )
}

function graphSeriesColor(index: number, series: DashboardGraphSeries): string {
  if (index === 0) return '#22d3ee'
  if (series.tone === 'error') return '#fb7185'
  if (series.tone === 'warning') return '#fbbf24'
  if (series.tone === 'healthy') return '#34d399'
  const palette = ['#60a5fa', '#a78bfa', '#f472b6', '#2dd4bf', '#c084fc', '#f59e0b', '#93c5fd']
  return palette[(index - 1) % palette.length]
}

function DashboardSparkline({ series, compact = false }: { series: DashboardGraphSeries[]; compact?: boolean }) {
  const activeSeries = series.filter(item => item.points.length >= 2)

  if (!activeSeries.length) {
    return (
      <div className="flex min-h-14 items-center justify-center rounded-xl border border-white/10 bg-black/20 text-xs text-white/45">
        Trend samples pending
      </div>
    )
  }

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
    <div className="min-h-0 rounded-xl border border-white/10 bg-black/20 px-2.5 py-2">
      <svg viewBox={`0 0 ${width} ${height}`} className="h-14 w-full overflow-visible md:h-16" role="img" aria-label="Trend lines">
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

function DashboardGraphVisual({ card, compact = false }: { card: DashboardCardSummary; compact?: boolean }) {
  if (card.series.length) return <DashboardSparkline series={card.series} compact={compact} />

  return (
    <div className="flex min-h-14 items-center justify-center rounded-xl border border-white/10 bg-black/20 text-xs text-white/45">
      No historical series yet
    </div>
  )
}

function DashboardCardVisual({ card }: { card: DashboardCardSummary }) {
  const metrics = dashboardMetricByKey(card.metrics)
  const metric = (key: string) => metrics.get(key)
  const value = (key: string) => {
    const selected = metric(key)
    return selected ? dashboardMetricNumber(selected) ?? 0 : 0
  }

  if (card.id === 'system.operations') {
    return (
      <VisualStack
        emptyLabel="No queued or stalled work"
        segments={[
          { key: 'pending', label: 'Pending', value: value('pending'), tone: dashboardSeverityTone('warning') },
          { key: 'in_progress', label: 'Active', value: value('in_progress'), tone: dashboardSeverityTone('info') },
          { key: 'stalled', label: 'Stalled', value: value('stalled'), tone: dashboardSeverityTone('error') },
          { key: 'failed_24h', label: 'Failed', value: value('failed_24h'), tone: dashboardSeverityTone('error') },
        ]}
      />
    )
  }

  if (card.id === 'system.threadpools') {
    const pressure = metric('pressure')
    return pressure ? <VisualMeter metric={pressure} /> : (
      <VisualStack
        emptyLabel="No queued worker pressure"
        segments={[
          { key: 'queue', label: 'Queue', value: value('queue'), tone: dashboardSeverityTone('warning') },
          { key: 'pressured', label: 'Pressured', value: value('pressured'), tone: dashboardSeverityTone('warning') },
          { key: 'saturated', label: 'Saturated', value: value('saturated'), tone: dashboardSeverityTone('error') },
        ]}
      />
    )
  }

  if (card.id === 'system.connections') {
    return (
      <VisualStack
        emptyLabel="No active websocket sessions"
        segments={[
          { key: 'human', label: 'Human', value: value('human'), tone: dashboardSeverityTone('info') },
          { key: 'share', label: 'Share', value: value('share'), tone: dashboardSeverityTone('healthy') },
          { key: 'unauthenticated', label: 'Unauth', value: value('unauthenticated'), tone: dashboardSeverityTone('warning') },
        ]}
      />
    )
  }

  if (card.id === 'system.storage') {
    return (
      <VisualStack
        emptyLabel="No vault backends configured"
        segments={[
          { key: 'local', label: 'Local', value: value('local'), tone: dashboardSeverityTone('info') },
          { key: 's3', label: 'S3', value: value('s3'), tone: dashboardSeverityTone('info') },
          { key: 'inactive', label: 'Inactive', value: value('inactive'), tone: dashboardSeverityTone('warning') },
        ]}
      />
    )
  }

  if (card.id === 'system.retention') {
    return (
      <VisualStack
        emptyLabel="Retention queues are clear"
        segments={[
          { key: 'overdue', label: 'Overdue', value: value('overdue'), tone: dashboardSeverityTone('error') },
          { key: 'cache_expired', label: 'Expired', value: value('cache_expired'), tone: dashboardSeverityTone('warning') },
          { key: 'sync_backlog', label: 'Sync', value: value('sync_backlog'), tone: dashboardSeverityTone('warning') },
          { key: 'audit_backlog', label: 'Audit', value: value('audit_backlog'), tone: dashboardSeverityTone('info') },
        ]}
      />
    )
  }

  if (card.id === 'system.trends') {
    const trendMetrics = ['latest_sample_age', 'window', 'coverage']
      .map(key => metric(key))
      .filter((item): item is DashboardMetricSummary => Boolean(item))

    return (
      <div className="rounded-xl border border-white/10 bg-black/20 px-2.5 py-2">
        <div className="flex items-center gap-1">
          {[0, 1, 2, 3, 4, 5].map(index => (
            <span
              key={index}
              className={[
                'h-1.5 flex-1 rounded-full',
                card.severity === 'error' ? 'bg-rose-300/65'
                : card.severity === 'warning' ? 'bg-amber-300/65'
                : index < trendMetrics.length + 2 ? 'bg-cyan-300/65'
                : 'bg-white/10',
              ].join(' ')}
            />
          ))}
        </div>
        <div className="mt-2 grid grid-cols-3 gap-1.5">
          {trendMetrics.map(item => (
            <div key={item.key} className="min-w-0 rounded-lg border border-white/10 bg-white/[0.03] px-2 py-1">
              <div className="truncate text-[8.5px] uppercase tracking-[0.06em] text-white/40">{item.label}</div>
              <div className="mt-1 truncate text-[12px] font-semibold leading-none text-white/75">{item.value}</div>
            </div>
          ))}
        </div>
      </div>
    )
  }

  const visualMetric =
    metric('error_rate') ??
    metric('hit_rate') ??
    metric('cache_hit') ??
    metric('pressure') ??
    metric('services') ??
    metric('protocols') ??
    metric('overdue') ??
    metric('latest_sample_age') ??
    metric('window')

  return visualMetric ? <VisualMeter metric={visualMetric} /> : null
}

function OverviewShell({
  children,
  className,
}: {
  children: React.ReactNode
  className?: string
}) {
  return (
    <section
      className={[
        'relative overflow-hidden rounded-3xl border border-white/10 bg-zinc-950/55 p-4 shadow-[0_20px_60px_-25px_rgba(0,0,0,0.9)] backdrop-blur-xl',
        'before:pointer-events-none before:absolute before:inset-0 before:bg-[radial-gradient(900px_circle_at_15%_10%,rgba(255,255,255,0.08),transparent_45%),radial-gradient(700px_circle_at_85%_25%,rgba(56,189,248,0.09),transparent_55%)]',
        className ?? '',
      ].join(' ')}>
      <div className="relative z-10">{children}</div>
    </section>
  )
}

function gridClassForSize(size: DashboardCardSize): string {
  if (size === '1x1') return 'md:col-span-3 lg:col-span-3 md:row-span-2'
  if (size === '1x2') return 'md:col-span-3 lg:col-span-3 md:row-span-4'
  if (size === '2x1') return 'md:col-span-6 lg:col-span-6 md:row-span-2'
  if (size === '2x2') return 'md:col-span-6 lg:col-span-6 md:row-span-4'
  if (size === '3x1') return 'md:col-span-6 lg:col-span-9 md:row-span-2'
  if (size === '3x2') return 'md:col-span-6 lg:col-span-9 md:row-span-4'
  return 'md:col-span-6 lg:col-span-12 md:row-span-4'
}

function baseHeightForCard(layoutCard: DashboardLayoutCard): string {
  if (layoutCard.size === '1x1') return 'min-h-[9.5rem]'
  if (layoutCard.size === '1x2') return 'min-h-[15rem]'
  if (layoutCard.size === '2x1') return 'min-h-[10rem]'
  if (layoutCard.size === '2x2') return 'min-h-[15rem]'
  if (layoutCard.size === '3x1') return 'min-h-[10rem]'
  if (layoutCard.size === '3x2') return 'min-h-[15rem]'
  return 'min-h-[15rem]'
}

const metricCapacityBySize: Record<DashboardCardSize, number> = {
  '1x1': 2,
  '1x2': 4,
  '2x1': 4,
  '2x2': 6,
  '3x1': 4,
  '3x2': 8,
  '4x2': 10,
}

function metricCountForCard(layoutCard: DashboardLayoutCard): number {
  if (layoutCard.variant === 'graph') {
    if (layoutCard.size === '1x1') return 1
    if (layoutCard.size === '1x2' || layoutCard.size === '2x1') return 2
    if (layoutCard.size === '2x2' || layoutCard.size === '3x1') return 3
    return 4
  }
  const hasVisual = layoutCard.variant === 'visual' || layoutCard.variant === 'hero'
  const capacity = metricCapacityBySize[layoutCard.size]
  if (!hasVisual) return capacity
  if (layoutCard.size === '1x1') return 2
  if (layoutCard.size === '2x1') return 3
  return capacity
}

function metricGridClassForCard(layoutCard: DashboardLayoutCard): string {
  if (layoutCard.variant === 'graph' && (layoutCard.size === '2x1' || layoutCard.size === '1x2')) return 'grid-cols-2'
  if (layoutCard.variant === 'graph' && (layoutCard.size === '2x2' || layoutCard.size === '3x1')) return 'grid-cols-3'
  if (layoutCard.variant === 'graph') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '4x2') return 'grid-cols-2 md:grid-cols-5'
  if (layoutCard.size === '3x2') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '3x1') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '2x2') return 'grid-cols-2 md:grid-cols-3'
  if (layoutCard.size === '2x1' && (layoutCard.variant === 'visual' || layoutCard.variant === 'hero')) return 'grid-cols-3'
  if (layoutCard.size === '2x1') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '1x2') return 'grid-cols-2'
  return 'grid-cols-2'
}

function issueCountForCard(layoutCard: DashboardLayoutCard): number {
  if (layoutCard.variant === 'hero' || layoutCard.size === '4x2') return 3
  if (layoutCard.variant === 'graph' || layoutCard.variant === 'visual' || layoutCard.variant === 'summary' || layoutCard.size === '2x2' || layoutCard.size === '3x2') return 2
  return 1
}

function pendingCardForLayout(layoutCard: DashboardLayoutCard, catalogItem: DashboardCardCatalogItem): DashboardCardSummary {
  return new DashboardCardSummary({
    id: catalogItem.id,
    section_id: catalogItem.sectionId,
    title: catalogItem.title,
    description: catalogItem.description,
    href: catalogItem.href,
    variant: layoutCard.variant,
    size: layoutCard.size,
    severity: 'unknown',
    available: true,
    summary: 'Waiting for backend summary.',
    metrics: [],
    warnings: [],
    errors: [],
    checked_at: null,
  })
}

function CardCustomizationControls({
  layoutCard,
  catalogItem,
  index,
  total,
  onMove,
  onDragStart,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  index: number
  total: number
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  const buttonClass =
    'rounded-full border border-white/10 bg-white/5 px-2 py-0.5 text-[11px] text-white/65 transition hover:border-cyan-200/30 hover:text-cyan-100 disabled:cursor-not-allowed disabled:opacity-35'
  const selectClass =
    'rounded-full border border-white/10 bg-zinc-950/80 px-2 py-0.5 text-[11px] text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50'

  return (
    <div className="mb-3 flex flex-wrap items-center gap-1.5 rounded-2xl border border-white/10 bg-black/20 p-1.5">
      <button
        type="button"
        draggable
        className="cursor-grab rounded-full border border-cyan-200/20 bg-cyan-400/10 px-2 py-0.5 text-[11px] text-cyan-100 transition hover:border-cyan-100/45 active:cursor-grabbing"
        title="Drag to reorder"
        onDragStart={event => onDragStart(layoutCard.instanceId, event)}>
        Drag
      </button>
      <button type="button" className={buttonClass} onClick={() => onMove(layoutCard.instanceId, -1)} disabled={index === 0}>
        Up
      </button>
      <button
        type="button"
        className={buttonClass}
        onClick={() => onMove(layoutCard.instanceId, 1)}
        disabled={index >= total - 1}>
        Down
      </button>
      <label className="flex items-center gap-2 text-xs text-white/45">
        Size
        <select
          className={selectClass}
          value={layoutCard.size}
          onChange={event => onSizeChange(layoutCard.instanceId, event.target.value as DashboardCardSize)}>
          {catalogItem.supportedSizes.map(size => (
            <option key={size} value={size}>
              {size}
            </option>
          ))}
        </select>
      </label>
      <label className="flex items-center gap-2 text-xs text-white/45">
        Variant
        <select
          className={selectClass}
          value={layoutCard.variant}
          onChange={event => onVariantChange(layoutCard.instanceId, event.target.value as DashboardCardVariant)}>
          {catalogItem.supportedVariants.map(variant => (
            <option key={variant} value={variant}>
              {variant}
            </option>
          ))}
        </select>
      </label>
      <button type="button" className={buttonClass} onClick={() => onRemove(layoutCard.instanceId)} disabled={total <= 1}>
        Remove
      </button>
    </div>
  )
}

function CardPickerPanel({
  catalog,
  visibleCombos,
  selectedSize,
  selectedVariant,
  onSizeChange,
  onVariantChange,
  onAdd,
}: {
  catalog: DashboardCardCatalogItem[]
  visibleCombos: Set<string>
  selectedSize: DashboardCardSize
  selectedVariant: DashboardCardVariant
  onSizeChange: (size: DashboardCardSize) => void
  onVariantChange: (variant: DashboardCardVariant) => void
  onAdd: (id: string, size: DashboardCardSize, variant: DashboardCardVariant) => void
}) {
  const candidates = catalog.filter(card => card.available)
  const sizeFiltered = candidates.filter(card => card.supportedSizes.includes(selectedSize))
  const cardsToShow = sizeFiltered.length ? sizeFiltered : candidates

  return (
    <div className="absolute right-0 top-[calc(100%+0.6rem)] z-40 w-[min(42rem,calc(100vw-2rem))] rounded-3xl border border-white/10 bg-zinc-950/95 p-3 shadow-[0_30px_90px_-30px_rgba(0,0,0,0.95)] backdrop-blur-xl">
      <div className="flex flex-wrap items-start justify-between gap-3 border-b border-white/10 pb-3">
        <div>
          <div className="text-sm font-semibold text-white">Add dashboard card</div>
          <div className="mt-0.5 text-xs text-white/50">Pick a card with the size and presentation you want.</div>
        </div>
        <div className="flex flex-wrap gap-1.5">
          {dashboardCardSizes.map(size => (
            <button
              key={size}
              type="button"
              className={[
                'rounded-full border px-2 py-0.5 text-[11px] transition',
                selectedSize === size ?
                  'border-cyan-200/40 bg-cyan-400/15 text-cyan-50'
                : 'border-white/10 bg-white/5 text-white/55 hover:border-cyan-200/25 hover:text-cyan-100',
              ].join(' ')}
              onClick={() => onSizeChange(size)}>
              {size}
            </button>
          ))}
          <span className="mx-1 h-5 w-px bg-white/10" />
          {dashboardCardVariants.map(variant => (
            <button
              key={variant}
              type="button"
              className={[
                'rounded-full border px-2 py-0.5 text-[11px] capitalize transition',
                selectedVariant === variant ?
                  'border-cyan-200/40 bg-cyan-400/15 text-cyan-50'
                : 'border-white/10 bg-white/5 text-white/55 hover:border-cyan-200/25 hover:text-cyan-100',
              ].join(' ')}
              onClick={() => onVariantChange(variant)}>
              {variant}
            </button>
          ))}
        </div>
      </div>

      <div className="mt-3 grid max-h-[28rem] grid-cols-1 gap-2 overflow-y-auto pr-1 md:grid-cols-2">
        {cardsToShow.length ?
          cardsToShow.map(card => {
            const size = card.supportedSizes.includes(selectedSize) ? selectedSize : card.defaultSize
            const variantAvailable = card.supportedVariants.includes(selectedVariant)
            const variant = variantAvailable ? selectedVariant : card.defaultVariant
            const tone = dashboardSeverityTone('unknown')
            const alreadyAdded = visibleCombos.has(`${card.id}:${variant}`)

            return (
              <button
                key={card.id}
                type="button"
                className={[
                  'group rounded-2xl border p-2.5 text-left transition',
                  alreadyAdded || !variantAvailable ?
                    'cursor-not-allowed border-white/10 bg-white/[0.025] opacity-55'
                  : 'border-white/10 bg-white/[0.035] hover:border-cyan-200/35 hover:bg-cyan-400/10',
                ].join(' ')}
                disabled={alreadyAdded || !variantAvailable}
                onClick={() => onAdd(card.id, size, variant)}>
                <div className="flex items-start justify-between gap-2">
                  <div className="min-w-0">
                    <div className="flex items-center gap-1.5 text-sm font-semibold text-white/90">
                      <DashboardSeverityIcon severity="unknown" className={['h-3.5 w-3.5', tone.text].join(' ')} />
                      <span className="truncate">{card.title}</span>
                    </div>
                    <p className="mt-1 line-clamp-2 text-xs leading-snug text-white/50">{card.description}</p>
                  </div>
                  <span className="shrink-0 rounded-full border border-white/10 bg-black/20 px-2 py-0.5 text-[10px] text-white/55">
                    {!variantAvailable ? `No ${selectedVariant}`
                    : alreadyAdded ? 'Already added'
                    : `${size} · ${variant}`}
                  </span>
                </div>
                <div className="mt-2 grid grid-cols-3 gap-1.5">
                  {['status', 'metric', 'detail'].map(item => (
                    <div key={item} className="rounded-lg border border-white/10 bg-black/20 px-2 py-1">
                      <div className="h-1 w-8 rounded-full bg-white/15" />
                      <div className="mt-1 h-2 w-12 rounded-full bg-white/10 group-hover:bg-cyan-200/20" />
                    </div>
                  ))}
                </div>
              </button>
            )
          })
        : <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-4 text-sm text-white/55">
            Every available dashboard card is already visible.
          </div>}
      </div>
    </div>
  )
}

function DashboardHomeCard({
  card,
  layoutCard,
  catalogItem,
  customizing,
  index,
  total,
  onMove,
  onDragStart,
  onDragOverCard,
  onDropCard,
  onDragEnd,
  onOpen,
  dragging,
  dragOver,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  card: DashboardCardSummary
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  customizing: boolean
  index: number
  total: number
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onDragOverCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDropCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDragEnd: () => void
  onOpen: (href: string) => void
  dragging: boolean
  dragOver: boolean
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  const tone = dashboardSeverityTone(card.severity)
  const errors = card.errors.length
  const warnings = card.warnings.length
  const firstIssue = sortDashboardIssues([...card.errors, ...card.warnings])[0]
  const isCompact = layoutCard.variant === 'compact' || layoutCard.size === '1x1' || layoutCard.size === '2x1'
  const isHero = layoutCard.variant === 'hero' || layoutCard.size === '4x2'
  const isGraph = layoutCard.variant === 'graph'
  const isVisual = layoutCard.variant === 'visual'
  const metricCount = metricCountForCard(layoutCard)
  const issueCount = issueCountForCard(layoutCard)
  const visualKeys =
    isGraph ? dashboardGraphMetricKeys(card.id)
    : isVisual || isHero ? dashboardVisualMetricKeys(card.id)
    : new Set<string>()
  const selectedMetrics = selectDashboardCardMetrics(card, layoutCard, metricCount, visualKeys)
  const visualMetricCount = card.metrics.filter(metric => visualKeys.has(metric.key)).length
  const hiddenMetricCount = Math.max(0, card.metrics.length - selectedMetrics.length - visualMetricCount)
  const visual =
    isGraph ? <DashboardGraphVisual card={card} compact={isCompact} />
    : isVisual || isHero ? <DashboardCardVisual card={card} />
    : null
  const metricGridClass = metricGridClassForCard(layoutCard)

  return (
    <div
      className={['col-span-1', gridClassForSize(layoutCard.size)].join(' ')}
      onDragOver={event => onDragOverCard(layoutCard.instanceId, event)}
      onDrop={event => onDropCard(layoutCard.instanceId, event)}
      onDragEnd={onDragEnd}>
      <article
        role={!customizing ? 'link' : undefined}
        tabIndex={!customizing ? 0 : undefined}
        onClick={() => {
          if (!customizing && !dragging) onOpen(card.href || catalogItem.href)
        }}
        onKeyDown={event => {
          if (customizing) return
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault()
            onOpen(card.href || catalogItem.href)
          }
        }}
        className={[
          'group flex h-full max-h-full min-h-0 flex-col overflow-hidden rounded-2xl border bg-zinc-950/48 backdrop-blur transition hover:-translate-y-0.5 hover:brightness-110',
          !customizing ? 'cursor-pointer focus:outline-none focus-visible:ring-2 focus-visible:ring-cyan-200/45' : '',
          isHero ? 'p-3.5'
          : isGraph ? 'p-3'
          : isVisual ? 'p-3'
          : 'p-2.5',
          baseHeightForCard(layoutCard),
          tone.border,
          tone.ring,
          dragging ? 'opacity-55' : '',
          dragOver ? 'outline outline-2 outline-cyan-200/60' : '',
          errors > 0 || warnings > 0 ? 'ring-1 ring-inset' : '',
          errors > 0 ? 'ring-rose-300/15'
          : warnings > 0 ? 'ring-amber-300/15'
          : 'ring-white/0',
        ].join(' ')}>
        {customizing ?
          <CardCustomizationControls
            layoutCard={layoutCard}
            catalogItem={catalogItem}
            index={index}
            total={total}
            onMove={onMove}
            onDragStart={onDragStart}
            onRemove={onRemove}
            onSizeChange={onSizeChange}
            onVariantChange={onVariantChange}
          />
        : null}

        <div className="flex items-start justify-between gap-2">
          <div className="min-w-0">
            <div
              draggable
              className={['flex cursor-grab items-center gap-1.5 font-semibold text-white/90 active:cursor-grabbing', isHero ? 'text-lg' : isCompact ? 'text-sm' : 'text-base'].join(' ')}
              title="Drag card header to reorder"
              onDragStart={event => onDragStart(layoutCard.instanceId, event)}>
              <DashboardSeverityIcon severity={card.severity} className={[isHero ? 'h-5 w-5' : 'h-4 w-4', tone.text].join(' ')} />
              <span className="truncate">{card.title}</span>
            </div>
            {isHero ?
              <div className="mt-1 line-clamp-2 text-xs text-white/50">{card.description || catalogItem.description}</div>
            : null}
            <DashboardIssueSummaryLine
              severity={errors > 0 ? 'error' : warnings > 0 ? 'warning' : card.severity}
              errorCount={errors}
              warningCount={warnings}
              firstIssue={firstIssue}
            />
          </div>
          <DashboardSeverityBadge severity={card.severity} errorCount={errors} warningCount={warnings} showCount />
        </div>

        <p className={['mt-1.5 text-xs leading-snug text-white/70', isCompact ? 'line-clamp-1' : 'line-clamp-2'].join(' ')}>
          {card.available ? card.summary : card.unavailable_reason}
        </p>

        {visual ?
          <div className={isHero ? 'mt-2.5' : 'mt-2'}>{visual}</div>
        : null}

        {selectedMetrics.length ?
          <div className={['mt-2 grid min-h-0 gap-1.5 overflow-hidden', metricGridClass].join(' ')}>
            {selectedMetrics.map(metric => (
              <DashboardMetricTile key={`${card.id}-${metric.key}`} metric={metric} dense={!isHero} />
            ))}
            {hiddenMetricCount > 0 && !isCompact ?
              <div className="rounded-lg border border-white/10 bg-white/[0.03] px-2 py-1 text-[10px] text-white/45">
                +{hiddenMetricCount} more
              </div>
            : null}
          </div>
        : null}

        {!isCompact && (card.errors.length || card.warnings.length) ?
          <div className="mt-2 overflow-hidden">
            <DashboardIssueList issues={[...card.errors, ...card.warnings]} max={issueCount} link={false} compact />
          </div>
        : null}

        <div className="mt-auto min-h-0" />
      </article>
    </div>
  )
}

function buildOverviewPayload(layout: DashboardLayoutCard[]): DashboardOverviewRequest {
  return {
    scope: 'system',
    mode: 'dashboard_home',
    cards: [
      { id: SYSTEM_HEALTH_CARD_ID, size: '3x2', variant: 'hero' },
      ...visibleDashboardLayoutCards(layout)
        .filter(card => card.id !== SYSTEM_HEALTH_CARD_ID)
        .map(card => ({
          id: card.id,
          size: card.size,
          variant: card.variant,
        })),
    ],
  }
}

function reorderLayoutBefore(layout: DashboardLayoutCard[], dragId: string, targetId: string): DashboardLayoutCard[] {
  if (dragId === targetId) return layout

  const visible = visibleDashboardLayoutCards(layout)
  const from = visible.findIndex(card => card.instanceId === dragId)
  const to = visible.findIndex(card => card.instanceId === targetId)
  if (from < 0 || to < 0) return layout

  const reordered = [...visible]
  const [moved] = reordered.splice(from, 1)
  reordered.splice(to, 0, moved)
  const orderByInstanceId = new Map(reordered.map((card, order) => [card.instanceId, order]))
  return layout.map(card => orderByInstanceId.has(card.instanceId) ? { ...card, order: orderByInstanceId.get(card.instanceId) ?? card.order } : card)
}

export default function DashboardOverviewComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const router = useRouter()
  const wrapper = useStatsStore(s => s.dashboardOverview)
  const startPolling = useStatsStore(s => s.startDashboardOverviewPolling)
  const refreshOverview = useStatsStore(s => s.refreshDashboardOverview)
  const preferenceLoading = useDashboardPreferencesStore(s => s.loading)
  const preferenceSaving = useDashboardPreferencesStore(s => s.saving)
  const preferenceError = useDashboardPreferencesStore(s => s.error)
  const loadPreference = useDashboardPreferencesStore(s => s.getPreference)
  const savePreference = useDashboardPreferencesStore(s => s.savePreference)
  const resetPreference = useDashboardPreferencesStore(s => s.resetPreference)
  const [customizing, setCustomizing] = useState(false)
  const [layoutLoaded, setLayoutLoaded] = useState(false)
  const [layout, setLayout] = useState<DashboardLayoutCard[]>(() => makeDefaultLayout())
  const [savedLayout, setSavedLayout] = useState<DashboardLayoutCard[]>(() => makeDefaultLayout())
  const [savedLayoutKey, setSavedLayoutKey] = useState(() => layoutKey(makeDefaultLayout()))
  const [needsMigrationSave, setNeedsMigrationSave] = useState(false)
  const [saveNotice, setSaveNotice] = useState<string | null>(null)
  const [cardPickerOpen, setCardPickerOpen] = useState(false)
  const [pickerSize, setPickerSize] = useState<DashboardCardSize>('2x1')
  const [pickerVariant, setPickerVariant] = useState<DashboardCardVariant>('visual')
  const [selectedPresetId, setSelectedPresetId] = useState('default')
  const [draggedCardId, setDraggedCardId] = useState<string | null>(null)
  const [dragOverCardId, setDragOverCardId] = useState<string | null>(null)
  const dragJustEndedRef = useRef(false)

  useEffect(() => {
    let cancelled = false
    const localLayout = loadStoredLayout()
    const localKey = layoutKey(localLayout)

    setLayout(localLayout)
    setSavedLayout(localLayout)
    setSavedLayoutKey(localKey)
    setLayoutLoaded(true)

    void loadPreference({ preference_key: DASHBOARD_HOME_PREFERENCE_KEY })
      .then(preference => {
        if (cancelled) return

        if (preference.exists && preference.layout) {
          const serverLayout = normalizeDashboardLayout(preference.layout, dashboardCardCatalog, makeDefaultLayout())
          const serverKey = layoutKey(serverLayout)
          setLayout(serverLayout)
          setSavedLayout(serverLayout)
          setSavedLayoutKey(serverKey)
          setNeedsMigrationSave(false)
          storeLayout(serverLayout)
          void refreshOverview(buildOverviewPayload(serverLayout))
          return
        }

        const hasLocalLayout = typeof window !== 'undefined' && Boolean(window.localStorage.getItem(DASHBOARD_LAYOUT_STORAGE_KEY))
        setNeedsMigrationSave(hasLocalLayout)
      })
      .catch(() => {
        if (!cancelled) setNeedsMigrationSave(false)
      })

    return () => {
      cancelled = true
    }
  }, [loadPreference, refreshOverview])

  const catalogById = useMemo(() => dashboardCardCatalogById(), [])
  const visibleLayout = useMemo(() => visibleDashboardLayoutCards(layout), [layout])
  const visibleIds = useMemo(() => new Set(visibleLayout.map(card => card.id)), [visibleLayout])
  const visibleCombos = useMemo(() => new Set(visibleLayout.map(card => `${card.id}:${card.variant}`)), [visibleLayout])
  const addableCatalog = useMemo(
    () => dashboardCardCatalog.filter(card => card.available && card.supportedVariants.some(variant => !visibleCombos.has(`${card.id}:${variant}`))),
    [visibleCombos],
  )
  const overviewPayload = useMemo(() => buildOverviewPayload(layout), [layout])
  const payloadKey = useMemo(() => JSON.stringify(overviewPayload.cards), [overviewPayload.cards])
  const currentLayoutKey = useMemo(() => layoutKey(layout), [layout])
  const layoutDirty = layoutLoaded && currentLayoutKey !== savedLayoutKey

  useEffect(() => {
    startPolling(intervalMs, overviewPayload)
  }, [startPolling, intervalMs, overviewPayload, payloadKey])

  const normalizeNextLayout = useCallback((next: DashboardLayoutCard[]) => {
    return normalizeDashboardLayout(next, dashboardCardCatalog, makeDefaultLayout())
  }, [])

  const updateLayout = useCallback((updater: (current: DashboardLayoutCard[]) => DashboardLayoutCard[]) => {
    setSaveNotice(null)
    setLayout(current => normalizeNextLayout(updater(current)))
  }, [normalizeNextLayout])

  const addCard = useCallback((id: string, size?: DashboardCardSize, variant?: DashboardCardVariant) => {
    const catalogItem = catalogById.get(id)
    if (!catalogItem) return
    const nextSize = size && catalogItem.supportedSizes.includes(size) ? size : catalogItem.defaultSize
    const nextVariant = variant && catalogItem.supportedVariants.includes(variant) ? variant : catalogItem.defaultVariant
    const nextInstanceId = dashboardLayoutInstanceId(id, nextVariant)

    updateLayout(current => {
      if (visibleDashboardLayoutCards(current).some(card => card.id === id && card.variant === nextVariant)) return current
      const nextOrder = visibleDashboardLayoutCards(current).length
      return [
        ...current,
        {
          instanceId: nextInstanceId,
          id,
          visible: true,
          order: nextOrder,
          size: nextSize,
          variant: nextVariant,
        },
      ]
    })
    setCardPickerOpen(false)
  }, [catalogById, updateLayout])

  const removeCard = useCallback((instanceId: string) => {
    updateLayout(current => {
      if (visibleDashboardLayoutCards(current).length <= 1) return current
      return current.filter(card => card.instanceId !== instanceId)
    })
  }, [updateLayout])

  const moveCard = useCallback((instanceId: string, direction: -1 | 1) => {
    updateLayout(current => {
      const visible = visibleDashboardLayoutCards(current)
      const index = visible.findIndex(card => card.instanceId === instanceId)
      const nextIndex = index + direction
      if (index < 0 || nextIndex < 0 || nextIndex >= visible.length) return current

      const reordered = [...visible]
      const [moved] = reordered.splice(index, 1)
      reordered.splice(nextIndex, 0, moved)
      const orderByInstanceId = new Map(reordered.map((card, order) => [card.instanceId, order]))
      return current.map(card => orderByInstanceId.has(card.instanceId) ? { ...card, order: orderByInstanceId.get(card.instanceId) ?? card.order } : card)
    })
  }, [updateLayout])

  const reorderCardBefore = useCallback((dragId: string, targetId: string) => {
    updateLayout(current => reorderLayoutBefore(current, dragId, targetId))
  }, [updateLayout])

  const persistLayout = useCallback(async (nextLayout: DashboardLayoutCard[], notice = 'Layout saved.') => {
    const normalized = normalizeNextLayout(nextLayout)
    const preferenceLayout = dashboardLayoutPreference(normalized)
    const preference = await savePreference({
      preference_key: DASHBOARD_HOME_PREFERENCE_KEY,
      layout: preferenceLayout,
    })
    const saved = preference.layout ?
      normalizeDashboardLayout(preference.layout, dashboardCardCatalog, makeDefaultLayout())
    : normalized

    const nextKey = layoutKey(saved)
    setLayout(saved)
    setSavedLayout(saved)
    setSavedLayoutKey(nextKey)
    setNeedsMigrationSave(false)
    setSaveNotice(notice)
    storeLayout(saved)
    void refreshOverview(buildOverviewPayload(saved))
  }, [normalizeNextLayout, refreshOverview, savePreference])

  const startCardDrag = useCallback((id: string, event: React.DragEvent<HTMLElement>) => {
    dragJustEndedRef.current = true
    setDraggedCardId(id)
    setDragOverCardId(null)
    event.dataTransfer.effectAllowed = 'move'
    event.dataTransfer.setData('text/plain', id)
  }, [])

  const dragOverCard = useCallback((id: string, event: React.DragEvent<HTMLDivElement>) => {
    if (!draggedCardId || draggedCardId === id) return
    event.preventDefault()
    event.dataTransfer.dropEffect = 'move'
    setDragOverCardId(id)
  }, [draggedCardId])

  const dropCard = useCallback((id: string, event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    const dragId = event.dataTransfer.getData('text/plain') || draggedCardId
    if (dragId) {
      if (customizing) {
        reorderCardBefore(dragId, id)
      } else {
        const nextLayout = normalizeNextLayout(reorderLayoutBefore(layout, dragId, id))
        setSaveNotice(null)
        setLayout(nextLayout)
        void persistLayout(nextLayout, 'Order saved.').catch(() => undefined)
      }
    }
    setDraggedCardId(null)
    setDragOverCardId(null)
    window.setTimeout(() => {
      dragJustEndedRef.current = false
    }, 250)
  }, [customizing, draggedCardId, layout, normalizeNextLayout, persistLayout, reorderCardBefore])

  const endCardDrag = useCallback(() => {
    setDraggedCardId(null)
    setDragOverCardId(null)
    window.setTimeout(() => {
      dragJustEndedRef.current = false
    }, 250)
  }, [])

  const changeCardSize = useCallback((instanceId: string, size: DashboardCardSize) => {
    updateLayout(current => current.map(card => card.instanceId === instanceId ? { ...card, size } : card))
  }, [updateLayout])

  const changeCardVariant = useCallback((instanceId: string, variant: DashboardCardVariant) => {
    updateLayout(current => current.map(card => {
      if (card.instanceId !== instanceId) return card
      if (current.some(other => other.instanceId !== instanceId && other.visible && other.id === card.id && other.variant === variant)) return card
      return {
        ...card,
        instanceId: dashboardLayoutInstanceId(card.id, variant),
        variant,
      }
    }))
  }, [updateLayout])

  const saveLayout = useCallback(async () => {
    await persistLayout(layout)
  }, [layout, persistLayout])

  const doneCustomizing = useCallback(async () => {
    if (layoutDirty || needsMigrationSave) await saveLayout()
    setCustomizing(false)
  }, [layoutDirty, needsMigrationSave, saveLayout])

  const cancelCustomizing = useCallback(() => {
    setLayout(savedLayout)
    setCustomizing(false)
    setSaveNotice(null)
    void refreshOverview(buildOverviewPayload(savedLayout))
  }, [refreshOverview, savedLayout])

  const resetLayout = useCallback(async () => {
    const defaults = makeDefaultLayout()
    await resetPreference({ preference_key: DASHBOARD_HOME_PREFERENCE_KEY })
    if (typeof window !== 'undefined') window.localStorage.removeItem(DASHBOARD_LAYOUT_STORAGE_KEY)
    setLayout(defaults)
    setSavedLayout(defaults)
    setSavedLayoutKey(layoutKey(defaults))
    setNeedsMigrationSave(false)
    setSaveNotice('Layout reset.')
    void refreshOverview(buildOverviewPayload(defaults))
  }, [refreshOverview, resetPreference])

  const applyPreset = useCallback((preset: DashboardLayoutPreset) => {
    const next = normalizeDashboardLayout(dashboardLayoutFromCards(preset.cards), dashboardCardCatalog, makeDefaultLayout())
    setSelectedPresetId(preset.id)
    setSaveNotice(null)
    setLayout(next)
    void refreshOverview(buildOverviewPayload(next))
  }, [refreshOverview])

  const openCard = useCallback((href: string) => {
    if (dragJustEndedRef.current) return
    router.push(href || '/dashboard')
  }, [router])

  const overview = wrapper.data
  const tone = dashboardSeverityTone(overview.overall_status)
  const checkedAt = formatCheckedAt(overview.checked_at, wrapper.lastUpdated)
  const cardsById = useMemo(() => new Map(overview.cards.map(card => [card.id, card])), [overview.cards])
  const requestedCardSummaries = useMemo(() => overview.cards.filter(card => card.id !== SYSTEM_HEALTH_CARD_ID), [overview.cards])
  const healthCard = cardsById.get(SYSTEM_HEALTH_CARD_ID)
  const healthMetricMap = useMemo(() => dashboardMetricByKey(healthCard?.metrics ?? []), [healthCard])
  const commandHealthMetrics = useMemo(
    () => ['services', 'protocols', 'deps']
      .map(key => healthMetricMap.get(key))
      .filter((metric): metric is DashboardMetricSummary => Boolean(metric)),
    [healthMetricMap],
  )
  const shellAdminMetric = healthMetricMap.get('shell_admin_uid')
  const shellSetupAdvisory = shellAdminMetric?.value === 'setup' ? shellAdminMetric : null
  const visibleCards = useMemo(() => {
    return visibleLayout
      .map((layoutCard, index) => {
        const catalogItem = catalogById.get(layoutCard.id)
        if (!catalogItem) return null
        const sequencedCard = requestedCardSummaries[index]
        const matchedCard = sequencedCard?.id === layoutCard.id && sequencedCard.variant === layoutCard.variant ?
          sequencedCard
        : requestedCardSummaries.find(card => card.id === layoutCard.id && card.variant === layoutCard.variant)
        return {
          layoutCard,
          catalogItem,
          card: matchedCard ?? pendingCardForLayout(layoutCard, catalogItem),
        }
      })
      .filter((item): item is { layoutCard: DashboardLayoutCard; catalogItem: DashboardCardCatalogItem; card: DashboardCardSummary } => Boolean(item))
  }, [catalogById, requestedCardSummaries, visibleLayout])
  const visibleAttention = useMemo(
    () => dedupeDashboardIssues(
      overview.attention.filter(issue => visibleIds.has(issue.card_id) || issue.card_id === SYSTEM_HEALTH_CARD_ID),
    ),
    [overview.attention, visibleIds],
  )
  const visibleAttentionPreview = useMemo(() => sortDashboardIssues(visibleAttention).slice(0, 3), [visibleAttention])
  const hiddenAttentionCount = Math.max(0, visibleAttention.length - visibleAttentionPreview.length)

  return (
    <div className="space-y-4">
      <OverviewShell className={tone.ring}>
        <div className="flex flex-col gap-4 xl:flex-row xl:items-start xl:justify-between">
          <div className="min-w-0">
            <div className="flex flex-wrap items-center gap-2">
              <h1 className="text-2xl font-semibold text-white">Health Command Center</h1>
              <DashboardSeverityBadge
                severity={overview.overall_status}
                errorCount={overview.error_count}
                warningCount={overview.warning_count}
                showCount
              />
              <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
                {overview.error_count} errors
              </span>
              <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
                {overview.warning_count} warnings
              </span>
              <LiveBadge loading={wrapper.loading || preferenceLoading} error={wrapper.error || preferenceError} lastUpdated={wrapper.lastUpdated} />
            </div>
            <p className="mt-2 max-w-3xl text-sm text-white/60">
              System Health is pinned here. Configure the operational cards below and use drilldowns for full detail.
            </p>
            {commandHealthMetrics.length ?
              <div className="mt-2 flex flex-wrap gap-2">
                {commandHealthMetrics.map(metric => (
                  <CommandHealthMetric key={metric.key} metric={metric} />
                ))}
              </div>
            : null}
            {shellSetupAdvisory ?
              <div className="mt-2 inline-flex items-center gap-2 rounded-full border border-cyan-300/25 bg-cyan-400/10 px-3 py-1.5 text-xs text-cyan-100">
                <DashboardSeverityIcon severity="info" className="h-3.5 w-3.5 text-cyan-100" />
                CLI shell admin UID is not configured.
              </div>
            : null}
          </div>

          <div className="flex shrink-0 flex-wrap items-center gap-2 xl:justify-end">
            <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
              checked {checkedAt}
            </span>
            {customizing ?
              <>
                <select
                  className="rounded-full border border-white/10 bg-zinc-950/80 px-2.5 py-1 text-xs text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50"
                  value={selectedPresetId}
                  onChange={event => {
                    const preset = dashboardLayoutPresets.find(item => item.id === event.target.value)
                    if (preset) applyPreset(preset)
                  }}>
                  {dashboardLayoutPresets.map(preset => (
                    <option key={preset.id} value={preset.id}>
                      {preset.title}
                    </option>
                  ))}
                </select>
                <div className="relative">
                  <button
                    type="button"
                    className="inline-flex items-center gap-1.5 rounded-full border border-cyan-200/25 bg-cyan-400/10 px-2.5 py-1 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45 disabled:cursor-not-allowed disabled:opacity-40"
                    onClick={() => setCardPickerOpen(open => !open)}
                    disabled={!addableCatalog.length}>
                    <GridPlusIcon className="h-3.5 w-3.5 fill-current" aria-hidden="true" />
                    Add Card
                  </button>
                  {cardPickerOpen ?
                    <CardPickerPanel
                      catalog={dashboardCardCatalog}
                      visibleCombos={visibleCombos}
                      selectedSize={pickerSize}
                      selectedVariant={pickerVariant}
                      onSizeChange={setPickerSize}
                      onVariantChange={setPickerVariant}
                      onAdd={addCard}
                    />
                  : null}
                </div>
                <button
                  type="button"
                  className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/70 transition hover:border-amber-200/35 hover:text-amber-100"
                  onClick={() => { void resetLayout().catch(() => undefined) }}
                  disabled={preferenceSaving}>
                  Reset
                </button>
                <button
                  type="button"
                  className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-2.5 py-1 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45 disabled:cursor-not-allowed disabled:opacity-40"
                  onClick={() => { void saveLayout().catch(() => undefined) }}
                  disabled={preferenceSaving || (!layoutDirty && !needsMigrationSave)}>
                  {preferenceSaving ? 'Saving...' : 'Save'}
                </button>
                <button
                  type="button"
                  className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/70 transition hover:border-white/25 hover:text-white"
                  onClick={cancelCustomizing}
                  disabled={preferenceSaving}>
                  Cancel
                </button>
                <button
                  type="button"
                  className="rounded-full border border-emerald-200/25 bg-emerald-400/10 px-2.5 py-1 text-xs font-medium text-emerald-100 transition hover:border-emerald-100/45"
                  onClick={() => { void doneCustomizing().catch(() => undefined) }}
                  disabled={preferenceSaving}>
                  {layoutDirty || needsMigrationSave ? 'Save & Done' : 'Done'}
                </button>
              </>
            : <button
                type="button"
                className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-3 py-1.5 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45"
                onClick={() => setCustomizing(true)}>
                Customize
              </button>
            }
          </div>
        </div>

        {visibleAttentionPreview.length ?
          <div className="mt-4 rounded-2xl border border-white/10 bg-black/20 p-2.5">
            <div className="mb-2 flex flex-wrap items-center justify-between gap-2 text-xs">
              <span className="font-semibold text-white/80">Attention</span>
              {hiddenAttentionCount > 0 ?
                <span className="text-white/45">+{hiddenAttentionCount} more</span>
              : null}
            </div>
            <DashboardIssueList issues={visibleAttentionPreview} max={3} compact className="grid grid-cols-1 gap-2 lg:grid-cols-3" />
          </div>
        : <div className="mt-4 inline-flex items-center gap-2 rounded-full border border-emerald-300/25 bg-emerald-400/10 px-3 py-1.5 text-xs text-emerald-100">
            <DashboardSeverityIcon severity="healthy" className="h-3.5 w-3.5 text-emerald-100" />
            No warning or error attention items
          </div>
        }

        {wrapper.error ?
          <div className="mt-3 rounded-2xl border border-rose-300/30 bg-rose-400/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        : null}

        {customizing ?
          <div className="mt-3 flex flex-wrap gap-2">
            <div className="rounded-full border border-cyan-300/25 bg-cyan-400/10 px-3 py-1.5 text-xs text-cyan-100/85">
              Drag handles or use Up/Down. Save stores this layout to your account.
            </div>
            {needsMigrationSave ?
              <div className="rounded-full border border-amber-300/25 bg-amber-400/10 px-3 py-1.5 text-xs text-amber-100/85">
                Browser-local layout loaded; save once to sync it.
              </div>
            : null}
            {layoutDirty ?
              <div className="rounded-full border border-amber-300/25 bg-amber-400/10 px-3 py-1.5 text-xs text-amber-100/85">
                Unsaved layout changes.
              </div>
            : null}
            {preferenceLoading ?
              <div className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-xs text-white/60">
                Loading saved preferences...
              </div>
            : null}
            {saveNotice ?
              <div className="rounded-full border border-emerald-300/25 bg-emerald-400/10 px-3 py-1.5 text-xs text-emerald-100">
                {saveNotice}
              </div>
            : null}
            {preferenceError ?
              <div className="rounded-full border border-rose-300/30 bg-rose-400/10 px-3 py-1.5 text-xs text-rose-100">
                {preferenceError}
              </div>
            : null}
          </div>
        : null}
      </OverviewShell>

      <div className="grid grid-cols-1 gap-3 md:auto-rows-[5rem] md:grid-flow-dense md:grid-cols-6 lg:grid-cols-12">
        {visibleCards.map(({ card, layoutCard, catalogItem }, index) => (
          <DashboardHomeCard
            key={layoutCard.instanceId}
            card={card}
            layoutCard={layoutCard}
            catalogItem={catalogItem}
            customizing={customizing}
            index={index}
            total={visibleCards.length}
            onMove={moveCard}
            onDragStart={startCardDrag}
            onDragOverCard={dragOverCard}
            onDropCard={dropCard}
            onDragEnd={endCardDrag}
            onOpen={openCard}
            dragging={draggedCardId === layoutCard.instanceId}
            dragOver={dragOverCardId === layoutCard.instanceId}
            onRemove={removeCard}
            onSizeChange={changeCardSize}
            onVariantChange={changeCardVariant}
          />
        ))}
      </div>
    </div>
  )
}
