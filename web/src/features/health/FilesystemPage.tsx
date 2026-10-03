'use client'

import React, { useState } from 'react'
import { Button } from '@/components/ui/Button'
import { useWs } from '@/lib/query'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { StatGrid, StatTile } from '@/components/ui/Stat'
import { formatBytes, formatInt, formatPercent } from '@/lib/format'
import { detailPoll, useHealthCards } from '@/features/health/hooks'
import { HealthSection, SubHeading } from '@/features/health/components'
import { BarList, Donut } from '@/features/health/charts'
import { bytes, int, list, ms, num, obj, pair, percent, text, type Raw } from '@/features/health/read'

const CARDS = ['system.fuse', 'system.fs_cache', 'system.http_cache']

interface FuseOp {
  op: string
  count: number | null
  errors: number | null
  alertable: number | null
  expected: number | null
  errorRate: number | null
  avgMs: number | null
  maxMs: number | null
  read: number | null
  written: number | null
}

const right = { className: 'text-right', headerClassName: 'text-right' }

const opColumns: Column<FuseOp>[] = [
  { key: 'op', header: 'Operation', cell: o => <span className="font-mono text-xs text-fg">{o.op}</span>, sortValue: o => o.op },
  { key: 'count', header: 'Calls', cell: o => <span className="tabular">{formatInt(o.count)}</span>, sortValue: o => o.count, ...right },
  {
    key: 'errors',
    header: 'Errors',
    cell: o => (
      <span className="tabular" title={`${formatInt(o.alertable)} alertable, ${formatInt(o.expected)} expected`}>
        {formatInt(o.errors)}
        {o.alertable ? <span className="text-fg-subtle"> ({formatInt(o.alertable)} alertable)</span> : null}
      </span>
    ),
    sortValue: o => o.errors,
    ...right,
  },
  { key: 'rate', header: 'Error rate', cell: o => <span className="tabular">{formatPercent(o.errorRate)}</span>, sortValue: o => o.errorRate, hideBelow: 'sm', ...right },
  { key: 'avg', header: 'Avg', cell: o => <span className="tabular">{ms(o.avgMs) ?? '—'}</span>, sortValue: o => o.avgMs, hideBelow: 'md', ...right },
  { key: 'max', header: 'Max', cell: o => <span className="tabular">{ms(o.maxMs) ?? '—'}</span>, sortValue: o => o.maxMs, hideBelow: 'md', ...right },
  { key: 'io', header: 'Read / written', cell: o => <span className="tabular">{`${formatBytes(o.read)} / ${formatBytes(o.written)}`}</span>, hideBelow: 'lg', ...right },
]

const FuseBody = ({ stats }: { stats: Raw }) => {
  const ops: FuseOp[] = list(stats.ops).map(o => ({
    op: text(o.op) ?? 'unknown',
    count: num(o.count),
    errors: num(o.errors),
    alertable: num(o.alertable_errors),
    expected: num(o.expected_errors),
    errorRate: num(o.error_rate),
    avgMs: num(o.avg_ms),
    maxMs: num(o.max_ms),
    read: num(o.bytes_read),
    written: num(o.bytes_written),
  }))
  const errnos = list(stats.top_errors)
  const [showIdle, setShowIdle] = useState(false)
  const idle = ops.filter(o => !o.count).length
  const rows = showIdle ? ops : ops.filter(o => o.count)
  return (
    <div className="space-y-6">
      <StatGrid className="xl:grid-cols-6">
        <StatTile label="Operations" value={int(stats.total_ops)} hint={num(stats.total_successes) !== null ? `${formatInt(stats.total_successes)} succeeded` : undefined} />
        <StatTile label="Alertable errors" value={percent(stats.alertable_error_rate, 2)} hint={int(stats.alertable_errors) ? `${int(stats.alertable_errors)} calls` : undefined} />
        <StatTile label="All errors" value={percent(stats.error_rate, 2)} hint={int(stats.total_errors) ? `${int(stats.total_errors)} calls, ${int(stats.expected_errors)} expected` : undefined} />
        <StatTile label="Open handles" value={int(stats.open_handles_current)} hint={int(stats.open_handles_peak) ? `peak ${int(stats.open_handles_peak)}` : undefined} />
        <StatTile label="Read" value={bytes(stats.read_bytes)} />
        <StatTile label="Written" value={bytes(stats.write_bytes)} />
      </StatGrid>
      <div className="grid gap-6 lg:grid-cols-[minmax(0,2.2fr)_minmax(0,1fr)]">
        <div className="min-w-0">
          <SubHeading>Operations since start</SubHeading>
          <DataTable rows={rows} columns={opColumns} rowKey={o => o.op} initialSort={{ key: 'count', dir: 'desc' }} empty="No FUSE operations recorded yet." />
          {idle ? (
            <Button variant="link" size="sm" className="mt-2" onClick={() => setShowIdle(v => !v)}>
              {showIdle ? 'Hide operations with no calls' : `Show ${idle} operations with no calls`}
            </Button>
          ) : null}
        </div>
        <div>
          <SubHeading>Errors by errno</SubHeading>
          <BarList
            emptyLabel="No errors recorded."
            items={errnos.map(e => ({
              key: `${e.errno_value}`,
              label: (
                <span>
                  <span className="font-mono text-xs text-fg">{text(e.name) ?? 'unknown'}</span>
                  {num(e.errno_value) !== null ? <span className="ml-1.5 text-fg-faint tabular">{`errno ${e.errno_value}`}</span> : null}
                </span>
              ),
              value: num(e.count),
              display: formatInt(e.count),
            }))}
          />
          <p className="mt-3 text-xs text-fg-subtle">Expected errors (a lookup of a missing name, for example) don&apos;t count toward the alertable rate.</p>
        </div>
      </div>
    </div>
  )
}

const CacheBody = ({ stats }: { stats: Raw }) => {
  const hits = num(stats.hits)
  const misses = num(stats.misses)
  const requests = hits !== null && misses !== null ? hits + misses : null
  const capacity = num(stats.capacity_bytes)
  const op = obj(stats.op)
  return (
    <div className="grid gap-6 lg:grid-cols-[minmax(0,1fr)_minmax(0,1.8fr)]">
      <Donut
        label="Cache hits and misses"
        data={[
          { label: 'Hits', value: hits },
          { label: 'Misses', value: misses },
        ]}
        center={requests ? formatPercent((hits ?? 0) / requests) : '—'}
        caption={requests ? 'hit rate' : 'no requests yet'}
      />
      <StatGrid className="sm:grid-cols-3 xl:grid-cols-3">
        <StatTile
          label="Used"
          value={bytes(stats.used_bytes)}
          hint={capacity ? `of ${formatBytes(capacity)}` : capacity === 0 ? 'capacity not reported' : undefined}
        />
        <StatTile label="Inserts" value={int(stats.inserts)} />
        <StatTile label="Evictions" value={int(stats.evictions)} />
        <StatTile label="Invalidations" value={int(stats.invalidations)} />
        <StatTile label="Read / written" value={pair(stats.bytes_read, stats.bytes_written) === null ? null : `${formatBytes(stats.bytes_read)} / ${formatBytes(stats.bytes_written)}`} />
        <StatTile label="Work ops" value={int(op.count)} hint={num(op.count) ? `avg ${ms(op.avg_ms)} · max ${ms(op.max_ms)}` : undefined} />
      </StatGrid>
    </div>
  )
}

export function FilesystemPage() {
  const { cards, pending } = useHealthCards(CARDS)
  const fuse = useWs('stats.system.fuse', null, detailPoll)
  const fsCache = useWs('stats.fs.cache', null, detailPoll)
  const httpCache = useWs('stats.http.cache', null, detailPoll)
  return (
    <div className="space-y-5">
      <HealthSection id="fuse" title="FUSE filesystem" card={cards.get('system.fuse')} cardPending={pending} query={fuse}>
        {data => <FuseBody stats={obj(data.stats)} />}
      </HealthSection>
      <div className="grid gap-5 2xl:grid-cols-2">
        <HealthSection id="fs-cache" title="FS cache" card={cards.get('system.fs_cache')} cardPending={pending} query={fsCache}>
          {data => <CacheBody stats={obj(data.stats)} />}
        </HealthSection>
        <HealthSection id="http-cache" title="Preview cache" card={cards.get('system.http_cache')} cardPending={pending} query={httpCache}>
          {data => <CacheBody stats={obj(data.stats)} />}
        </HealthSection>
      </div>
    </div>
  )
}
