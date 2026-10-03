'use client'

import React, { useMemo, useState } from 'react'
import { keepPreviousData } from '@tanstack/react-query'
import { useWs } from '@/lib/query'
import { Badge } from '@/components/ui/Badge'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { Field, Select } from '@/components/ui/Field'
import { Meter, StatGrid, StatTile } from '@/components/ui/Stat'
import { Segmented } from '@/components/ui/Tabs'
import { formatBytes, formatDateTime, formatInt, formatRelative, titleCase } from '@/lib/format'
import { detailPoll, useHealthCards } from '@/features/health/hooks'
import { HealthSection, SubHeading } from '@/features/health/components'
import { BarList, SeriesLegend, TimeSeriesChart, type ChartSeries } from '@/features/health/charts'
import { statusTone } from '@/features/health/model'
import { int, list, num, obj, seconds, text, type Raw } from '@/features/health/read'

const CARDS = ['system.operations', 'system.trends']
// Snapshots are written every five minutes; polling the (large) trend payload faster than that buys nothing.
// 7 days is the default: daemons through 1.8 answer a 24 h window with raw per-minute samples (~4x the bytes).
const TRENDS_POLL_MS = 300_000

const errorColumns: Column<Raw>[] = [
  {
    key: 'when',
    header: 'When',
    cell: e => <span className="whitespace-nowrap tabular" title={formatDateTime(e.occurred_at)}>{formatRelative(e.occurred_at)}</span>,
    sortValue: e => num(e.occurred_at) ?? text(e.occurred_at),
  },
  { key: 'op', header: 'Operation', cell: e => <span className="text-fg">{titleCase(text(e.operation) ?? 'unknown')}</span> },
  {
    key: 'status',
    header: 'Status',
    cell: e => (
      <Badge tone={statusTone(text(e.status))} dot>
        {titleCase(text(e.status) ?? 'unknown')}
      </Badge>
    ),
  },
  {
    key: 'path',
    header: 'Path',
    cell: e => <span className="block max-w-[22rem] truncate font-mono text-xs" title={text(e.path) ?? undefined}>{text(e.path) ?? text(e.target) ?? '—'}</span>,
    hideBelow: 'md',
  },
  { key: 'error', header: 'Error', cell: e => <span className="text-fg-muted">{text(e.error) ?? '—'}</span>, hideBelow: 'sm' },
]

const OperationsBody = ({ stats }: { stats: Raw }) => {
  const byStatus = obj(stats.operations_by_status)
  const byType = obj(stats.operations_by_type)
  const received = num(stats.upload_bytes_received_active)
  const expected = num(stats.upload_bytes_expected_active)
  const errors = list(stats.recent_operation_errors)
  const threshold = num(stats.stale_threshold_seconds)
  return (
    <div className="space-y-6">
      <StatGrid className="xl:grid-cols-6">
        <StatTile label="Queued" value={int(stats.pending_operations)} hint={seconds(stats.oldest_pending_operation_age_seconds) ? `oldest ${seconds(stats.oldest_pending_operation_age_seconds)}` : undefined} />
        <StatTile label="In progress" value={int(stats.in_progress_operations)} hint={seconds(stats.oldest_in_progress_operation_age_seconds) ? `oldest ${seconds(stats.oldest_in_progress_operation_age_seconds)}` : undefined} />
        <StatTile label="Stalled" value={int(stats.stalled_operations)} hint={threshold ? `no progress for ${seconds(threshold)}` : undefined} />
        <StatTile label="Failed (24 h)" value={int(stats.failed_operations_24h)} />
        <StatTile label="Cancelled (24 h)" value={int(stats.cancelled_operations_24h)} />
        <StatTile label="Share uploads" value={int(stats.active_share_uploads)} hint={int(stats.stalled_share_uploads) ? `${int(stats.stalled_share_uploads)} stalled · ${int(stats.failed_share_uploads_24h)} failed (24 h)` : undefined} />
      </StatGrid>

      <div className="grid gap-6 lg:grid-cols-3">
        <div>
          <SubHeading>Operations by status</SubHeading>
          <BarList
            emptyLabel="No operations recorded."
            items={Object.keys(byStatus).map(key => ({ key, label: titleCase(key), value: num(byStatus[key]), display: formatInt(byStatus[key]) }))}
          />
        </div>
        <div>
          <SubHeading>Operations by type</SubHeading>
          <BarList
            emptyLabel="No operations recorded."
            items={Object.keys(byType).map(key => ({ key, label: titleCase(key), value: num(byType[key]), display: formatInt(byType[key]) }))}
          />
        </div>
        <div>
          <SubHeading>Active share uploads</SubHeading>
          {received !== null && expected !== null && expected > 0 ? (
            <>
              <Meter ratio={received / expected} label="Share upload progress" />
              <p className="mt-2 text-sm text-fg-muted tabular">
                {formatBytes(received)} of {formatBytes(expected)}
                {seconds(stats.oldest_active_upload_age_seconds) ? ` · oldest ${seconds(stats.oldest_active_upload_age_seconds)}` : ''}
              </p>
            </>
          ) : (
            <p className="text-sm text-fg-subtle">{num(stats.active_share_uploads) === 0 ? 'No share uploads in flight.' : 'Upload progress is not available.'}</p>
          )}
        </div>
      </div>

      <div>
        <SubHeading>Recent failures</SubHeading>
        <DataTable rows={errors} columns={errorColumns} rowKey={e => `${e.occurred_at}-${e.path}-${e.operation}`} empty="No failed or cancelled operations recently." />
      </div>
    </div>
  )
}

// Curated trend charts. Each groups series that share a unit; anything else is one pick away in "Any series".
const CHARTS: { title: string; keys: string[] }[] = [
  { title: 'Operation queue', keys: ['operations_pending', 'operations_in_progress', 'operations_stalled'] },
  { title: 'Failed operations (24 h)', keys: ['operations_failed_24h'] },
  { title: 'Thread pool pressure', keys: ['threadpool_pressure'] },
  { title: 'Thread pool queue depth', keys: ['threadpool_queue_depth'] },
  { title: 'FUSE operations', keys: ['fuse_ops_per_second'] },
  { title: 'FUSE error rate', keys: ['fuse_error_rate', 'fuse_raw_error_rate'] },
  { title: 'FUSE latency', keys: ['fuse_latency_avg_ms'] },
  { title: 'FUSE throughput', keys: ['fuse_read_bytes_per_second', 'fuse_write_bytes_per_second'] },
  { title: 'Cache hit rate', keys: ['fs_cache_hit_rate', 'db_cache_hit_ratio'] },
  { title: 'Cache usage', keys: ['fs_cache_used_bytes', 'http_cache_used_bytes'] },
  { title: 'Database size', keys: ['db_size_bytes'] },
  { title: 'Database connections', keys: ['db_connections_total', 'db_connections_active'] },
  { title: 'Sessions', keys: ['connections_active_ws', 'connections_human', 'connections_unauthenticated'] },
  { title: 'Cleanup backlog', keys: ['retention_trash_overdue_count', 'retention_sync_backlog', 'retention_audit_backlog'] },
]

interface TrendSeries extends ChartSeries {
  unit: string
}

const ChartCard = ({ title, series }: { title: string; series: TrendSeries[] }) => (
  <div className="min-w-0 rounded-card border border-line bg-surface-1 p-3.5">
    <div className="mb-2 flex flex-wrap items-baseline justify-between gap-x-3 gap-y-1">
      <h3 className="text-sm font-medium text-fg">{title}</h3>
      <SeriesLegend series={series} />
    </div>
    <TimeSeriesChart series={series} unit={series[0]?.unit ?? ''} height={150} label={title} />
  </div>
)

const TrendsBody = ({ stats }: { stats: Raw }) => {
  const [extra, setExtra] = useState('')
  const series = useMemo(() => {
    const map = new Map<string, TrendSeries>()
    for (const s of list(stats.series)) {
      const key = text(s.key)
      if (!key) continue
      map.set(key, {
        key,
        label: text(s.label) ?? key,
        unit: text(s.unit) ?? '',
        points: list(s.points)
          .map(p => ({ t: num(p.created_at), v: num(p.value) }))
          .filter((p): p is { t: number; v: number } => p.t !== null && p.v !== null),
      })
    }
    return map
  }, [stats.series])

  const charts = CHARTS.map(chart => {
    const found = chart.keys.map(k => series.get(k)).filter((s): s is TrendSeries => Boolean(s))
    const unit = found[0]?.unit
    return { title: chart.title, series: found.filter(s => s.unit === unit) }
  }).filter(chart => chart.series.length)
  const extraSeries = extra ? series.get(extra) : undefined

  if (!series.size)
    return (
      <p className="py-8 text-center text-sm text-fg-subtle">
        No snapshots in this window yet. The daemon records one every few minutes while stats snapshots are enabled.
      </p>
    )

  return (
    <div className="space-y-4">
      <div className="grid gap-3 md:grid-cols-2 2xl:grid-cols-3">
        {charts.map(chart => (
          <ChartCard key={chart.title} title={chart.title} series={chart.series} />
        ))}
      </div>
      <div className="flex flex-wrap items-end gap-3 border-t border-line pt-4">
        <Field label="Any series" htmlFor="trend-extra" className="w-full sm:w-80">
          <Select id="trend-extra" value={extra} onChange={event => setExtra(event.target.value)}>
            <option value="">Choose a series…</option>
            {[...series.values()]
              .sort((a, b) => a.label.localeCompare(b.label))
              .map(s => (
                <option key={s.key} value={s.key}>
                  {s.label}
                </option>
              ))}
          </Select>
        </Field>
        <p className="pb-2 text-xs text-fg-subtle tabular">{series.size} series recorded in this window</p>
      </div>
      {extraSeries ? <ChartCard title={extraSeries.label} series={[extraSeries]} /> : null}
    </div>
  )
}

export function ActivityPage() {
  const { cards, pending } = useHealthCards(CARDS)
  const [windowHours, setWindowHours] = useState<'24' | '168'>('168')
  const operations = useWs('stats.system.operations', null, detailPoll)
  const trends = useWs('stats.system.trends', { window_hours: Number(windowHours) }, {
    refetchInterval: TRENDS_POLL_MS,
    staleTime: 60_000,
    placeholderData: keepPreviousData,
  })
  return (
    <div className="space-y-5">
      <HealthSection id="operation-queue" title="Operation queue" card={cards.get('system.operations')} cardPending={pending} query={operations}>
        {data => <OperationsBody stats={obj(data.stats)} />}
      </HealthSection>
      <HealthSection
        id="trends"
        title="Trends"
        card={cards.get('system.trends')}
        cardPending={pending}
        query={trends}
        actions={
          <Segmented
            label="Trend window"
            value={windowHours}
            onChange={setWindowHours}
            options={[
              { value: '24', label: '24 h' },
              { value: '168', label: '7 days' },
            ]}
          />
        }>
        {data => <TrendsBody stats={obj(data.stats)} />}
      </HealthSection>
    </div>
  )
}
