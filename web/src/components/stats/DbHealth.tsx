'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { DbHealthStatus } from '@/models/stats/dbStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones: Record<DbHealthStatus, Tone> = {
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

function formatInt(n: number): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function formatPercent(value: number | null): string {
  if (value == null || !Number.isFinite(value)) return 'unknown'
  return `${(value * 100).toFixed(2)}%`
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

const TableRow = ({
  name,
  totalBytes,
  tableBytes,
  indexBytes,
  rows,
  maxBytes,
}: {
  name: string
  totalBytes: number
  tableBytes: number
  indexBytes: number
  rows: number
  maxBytes: number
}) => {
  const width = `${Math.min(100, Math.max(3, maxBytes > 0 ? (totalBytes / maxBytes) * 100 : 3))}%`

  return (
    <div className="grid grid-cols-1 gap-2 rounded-2xl border border-white/10 bg-white/5 p-3 text-xs md:grid-cols-[1fr_110px_110px_110px_90px]">
      <div className="min-w-0">
        <div className="truncate font-semibold text-white/85">{name}</div>
        <div className="mt-2 h-1.5 overflow-hidden rounded-full bg-white/10">
          <div className="h-full rounded-full bg-cyan-300/70" style={{ width }} />
        </div>
      </div>
      <Metric label="total" value={formatBytes(totalBytes)} />
      <Metric label="table" value={formatBytes(tableBytes)} />
      <Metric label="indexes" value={formatBytes(indexBytes)} />
      <Metric label="rows" value={formatInt(rows)} />
    </div>
  )
}

const Metric = ({ label, value }: { label: string; value: React.ReactNode }) => (
  <div className="min-w-0">
    <div className="text-[11px] text-white/40 uppercase">{label}</div>
    <div className="mt-1 truncate text-sm font-semibold text-white/80">{value}</div>
  </div>
)

export default function DbHealthComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.dbStats)
  const startPolling = useStatsStore(s => s.startDbStatsPolling)
  const stopPolling = useStatsStore(s => s.stopDbStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const stats = wrapper.data
  const tone = tones[stats.status] ?? tones.unknown
  const checkedAt = formatCheckedAt(stats.checked_at, wrapper.lastUpdated)
  const connectionUsage = stats.connectionUsage()
  const maxTableBytes = Math.max(0, ...stats.largest_tables.map(table => table.total_bytes))
  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Database Health"
      subtitle="PostgreSQL connection, cache, transaction, and table-size signals"
      right={right}
      className="w-full">
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Status" value={stats.status} detail={stats.database_name ?? 'unknown db'} tone={tone} />
          <SummaryPill
            label="DB size"
            value={formatBytes(stats.db_size_bytes)}
            detail={`checked ${checkedAt}`}
            tone={stats.connected ? tones.healthy : tones.critical}
          />
          <SummaryPill
            label="Connections"
            value={`${formatInt(stats.connections_total)}${stats.connections_max ? ` / ${formatInt(stats.connections_max)}` : ''}`}
            detail={`${formatInt(stats.connections_active)} active / ${formatInt(stats.connections_idle)} idle`}
            tone={connectionUsage != null && connectionUsage >= 0.8 ? tones.warning : tones.healthy}
          />
          <SummaryPill
            label="Cache hit"
            value={formatPercent(stats.cache_hit_ratio)}
            detail="pg_stat_database"
            tone={stats.cache_hit_ratio != null && stats.cache_hit_ratio < 0.8 ? tones.warning : tones.healthy}
          />
          <SummaryPill
            label="Oldest tx"
            value={formatDuration(stats.oldest_transaction_age_seconds)}
            detail={`${formatInt(stats.connections_idle_in_transaction)} idle tx`}
            tone={
              stats.oldest_transaction_age_seconds != null && stats.oldest_transaction_age_seconds >= 3600 ?
                tones.warning
              : tones.healthy
            }
          />
          <SummaryPill
            label="Slow queries"
            value={stats.pg_stat_statements_enabled ? formatInt(stats.slow_query_count ?? 0) : 'not enabled'}
            detail={stats.pg_stat_statements_enabled ? '>= 1s mean exec' : 'pg_stat_statements'}
            tone={stats.pg_stat_statements_enabled && (stats.slow_query_count ?? 0) > 0 ? tones.warning : tones.unknown}
          />
        </div>

        {(wrapper.error || stats.error) ?
          <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs text-rose-200/90">
            {wrapper.error ?? stats.error}
          </div>
        : null}

        <div className="grid grid-cols-1 gap-3 md:grid-cols-3">
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
            <div className="text-xs font-semibold text-white/70 uppercase">Deadlocks</div>
            <div className={['mt-2 text-2xl font-semibold', stats.deadlocks > 0 ? tones.warning.text : 'text-white/85'].join(' ')}>
              {formatInt(stats.deadlocks)}
            </div>
          </div>
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
            <div className="text-xs font-semibold text-white/70 uppercase">Temp bytes</div>
            <div className="mt-2 text-2xl font-semibold text-white/85">{formatBytes(stats.temp_bytes)}</div>
          </div>
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
            <div className="text-xs font-semibold text-white/70 uppercase">Extension</div>
            <div className="mt-2 text-sm font-semibold text-white/85">
              pg_stat_statements {stats.pg_stat_statements_enabled ? 'enabled' : 'not enabled'}
            </div>
          </div>
        </div>

        <div className="space-y-2">
          <div className="text-xs font-semibold text-white/70 uppercase">Largest tables</div>
          {stats.largest_tables.length ? (
            <div className="space-y-2">
              {stats.largest_tables.map(table => (
                <TableRow
                  key={table.table_name}
                  name={table.table_name}
                  totalBytes={table.total_bytes}
                  tableBytes={table.table_bytes}
                  indexBytes={table.index_bytes}
                  rows={table.row_estimate}
                  maxBytes={maxTableBytes}
                />
              ))}
            </div>
          ) : (
            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-4 text-sm text-white/55">
              No user table size statistics reported.
            </div>
          )}
        </div>
      </div>
    </StatsCard>
  )
}
