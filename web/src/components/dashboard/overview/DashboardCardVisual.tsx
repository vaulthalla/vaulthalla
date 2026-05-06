'use client'

import type { DashboardCardSummary, DashboardMetricSummary } from '@/models/stats/dashboardOverview'
import type { DashboardCardVisualKind } from '@/components/dashboard/dashboardCardDefinitions'
import {
  dashboardMetricByKey,
  dashboardMetricMeterValue,
  dashboardMetricNumber,
} from '@/components/dashboard/dashboardMetricCuration'
import { dashboardSeverityTone } from '@/components/dashboard/dashboardSeverity'
import { DashboardSparkline } from '@/components/dashboard/overview/DashboardSparkline'

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
    <div className={['flex h-full flex-col justify-center rounded-xl border px-2.5 py-2', tone.border, tone.bg].join(' ')}>
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
    <div className="flex h-full flex-col justify-center rounded-xl border border-white/10 bg-black/20 px-2.5 py-2">
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

function DashboardGraphVisual({ card, compact = false }: { card: DashboardCardSummary; compact?: boolean }) {
  if (card.series.length) return <DashboardSparkline series={card.series} compact={compact} />

  return (
    <div className="flex h-full min-h-14 items-center justify-center rounded-xl border border-white/10 bg-black/20 text-xs text-white/45">
      No historical series yet
    </div>
  )
}

export function DashboardCardVisual({
  card,
  visualKind,
  compact = false,
}: {
  card: DashboardCardSummary
  visualKind: DashboardCardVisualKind
  compact?: boolean
}) {
  const metrics = dashboardMetricByKey(card.metrics)
  const metric = (key: string) => metrics.get(key)
  const value = (key: string) => {
    const selected = metric(key)
    return selected ? dashboardMetricNumber(selected) ?? 0 : 0
  }

  if (visualKind.startsWith('sparkline:')) return <DashboardGraphVisual card={card} compact={compact} />

  if (visualKind === 'stack:operations') {
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

  if (visualKind === 'meter:thread_pressure') {
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

  if (visualKind === 'stack:connections') {
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

  if (visualKind === 'stack:storage') {
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

  if (visualKind === 'stack:retention') {
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

  if (visualKind === 'trend:coverage') {
    const trendMetrics = ['latest_sample_age', 'window', 'coverage']
      .map(key => metric(key))
      .filter((item): item is DashboardMetricSummary => Boolean(item))

    return (
      <div className="flex h-full flex-col justify-center rounded-xl border border-white/10 bg-black/20 px-2.5 py-2">
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
    visualKind === 'meter:fuse_error' ? metric('error_rate')
    : visualKind === 'meter:cache_hit' ? metric('hit_rate')
    : visualKind === 'meter:db_cache' ? metric('cache_hit')
    : metric('error_rate') ??
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
