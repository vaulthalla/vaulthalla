import React, { useEffect, useMemo, useRef } from 'react'
import * as echarts from 'echarts/core'

import type { IEvent } from '@/models/stats/sync'
import { StatsCard } from '@/components/stats/StatsCard'

type LatestSyncHealthProps = { event: IEvent | null | undefined; title?: string }
type MetricSummary = { ops: number; bytes: number; ms: number; failed: number }
type PieTooltipDatum = { name?: string; value?: number }
type EventCompat = IEvent & {
  conflicts?: Array<{ resolved_at?: string | null }>
  throughputs?: Array<{
    metric_type?: string
    num_ops?: number
    failed_ops?: number
    size_bytes?: number
    duration_ms?: number
  }>
  run_uuid?: string
  vault_id?: number
}

const clampNonNeg = (n: number) => (Number.isFinite(n) ? Math.max(0, n) : 0)

function bytes(n: number): string {
  const v = clampNonNeg(n)
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB']
  let i = 0
  let x = v
  while (x >= 1024 && i < units.length - 1) {
    x /= 1024
    i++
  }
  const digits =
    i === 0 ? 0
    : x < 10 ? 2
    : x < 100 ? 1
    : 0
  return `${x.toFixed(digits)} ${units[i]}`
}

function fmtMs(ms: number): string {
  const v = clampNonNeg(ms)
  if (v < 1000) return `${Math.round(v)}ms`
  const s = v / 1000
  if (s < 60) return `${s.toFixed(s < 10 ? 2 : 1)}s`
  const m = Math.floor(s / 60)
  const rs = Math.floor(s % 60)
  if (m < 60) return `${m}m ${rs}s`
  const h = Math.floor(m / 60)
  const rm = m % 60
  return `${h}h ${rm}m`
}

function parseIsoMs(s?: string | null): number | null {
  if (!s) return null
  const t = Date.parse(s)
  return Number.isFinite(t) ? t : null
}

function relTimeFromNow(msUtc: number, now = Date.now()): string {
  const d = Math.max(0, now - msUtc)
  if (d < 5_000) return 'just now'
  if (d < 60_000) return `${Math.round(d / 1000)}s ago`
  if (d < 3_600_000) return `${Math.round(d / 60_000)}m ago`
  if (d < 86_400_000) return `${Math.round(d / 3_600_000)}h ago`
  return `${Math.round(d / 86_400_000)}d ago`
}

function pct(part: number, whole: number): string {
  const w = clampNonNeg(whole)
  if (w <= 0) return '—'
  return `${((clampNonNeg(part) / w) * 100).toFixed(1)}%`
}

const Pill = ({
  label,
  value,
  sub,
  tone = 'neutral',
}: {
  label: string
  value: string
  sub?: string
  tone?: 'good' | 'warn' | 'bad' | 'neutral'
}) => {
  const toneClass =
    tone === 'good' ? 'border-emerald-400/20 bg-emerald-400/10'
    : tone === 'warn' ? 'border-amber-400/20 bg-amber-400/10'
    : tone === 'bad' ? 'border-rose-400/20 bg-rose-400/10'
    : 'border-white/10 bg-white/5'

  return (
    <div className={`rounded-2xl border px-3 py-2 backdrop-blur ${toneClass}`}>
      <div className="text-[11px] text-white/55">{label}</div>
      <div className="text-sm leading-5 font-semibold text-white/90">{value}</div>
      {sub ?
        <div className="text-[11px] text-white/45">{sub}</div>
      : null}
    </div>
  )
}

function statusTone(status?: string | null): 'good' | 'warn' | 'bad' | 'neutral' {
  if (!status) return 'neutral'
  if (status === 'success') return 'good'
  if (status === 'running' || status === 'pending') return 'neutral'
  if (status === 'stalled') return 'warn'
  if (status === 'error' || status === 'canceled') return 'bad'
  return 'neutral'
}

function heartbeatTone(heartbeatAtMs: number | null, now = Date.now()): 'good' | 'warn' | 'bad' | 'neutral' {
  if (heartbeatAtMs == null) return 'neutral'
  const age = now - heartbeatAtMs
  if (age < 30_000) return 'good'
  if (age < 2 * 60_000) return 'warn'
  return 'bad'
}

function metricLabel(k: string): string {
  // tiny polish, optional
  switch (k) {
    case 'upload':
      return 'Upload'
    case 'download':
      return 'Download'
    case 'rename':
      return 'Rename'
    case 'delete':
      return 'Delete'
    case 'copy':
      return 'Copy'
    default:
      return k
  }
}

export default function LatestSyncHealth({ event, title = 'Latest sync health' }: LatestSyncHealthProps) {
  const chartsRef = useRef<HTMLDivElement | null>(null)
  const chartRef = useRef<echarts.EChartsType | null>(null)

  const d = useMemo(() => {
    if (!event) return null
    const eventData = event as EventCompat

    const beginMs = parseIsoMs(eventData.timestamp_begin)
    const endMs = parseIsoMs(eventData.timestamp_end)
    const hbMs = parseIsoMs(eventData.heartbeat_at)

    const now = Date.now()
    const durationMs =
      beginMs != null ?
        endMs != null ?
          endMs - beginMs
        : now - beginMs
      : 0

    const opsTotal = clampNonNeg(eventData.num_ops_total ?? 0)
    const opsFailed = clampNonNeg(eventData.num_failed_ops ?? 0)
    const opsOk = Math.max(0, opsTotal - opsFailed)

    const bytesUp = clampNonNeg(eventData.bytes_up ?? 0)
    const bytesDown = clampNonNeg(eventData.bytes_down ?? 0)

    const conflicts = Array.isArray(eventData.conflicts) ? eventData.conflicts : []
    const numConflicts = clampNonNeg(eventData.num_conflicts ?? conflicts.length ?? 0)
    const unresolved = conflicts.filter(conflict => !conflict?.resolved_at).length

    const divergence = eventData.divergence_detected
    const localHash = eventData.local_state_hash ?? null
    const remoteHash = eventData.remote_state_hash ?? null
    const configHash = eventData.config_hash ?? null
    const hashMismatch = localHash && remoteHash ? localHash !== remoteHash : false

    // Throughput aggregation
    const th = Array.isArray(eventData.throughputs) ? eventData.throughputs : []
    const byMetric = new Map<string, MetricSummary>()
    for (const t of th) {
      const k = String(t?.metric_type ?? 'unknown')
      const cur = byMetric.get(k) ?? { ops: 0, bytes: 0, ms: 0, failed: 0 }
      cur.ops += clampNonNeg(Number(t?.num_ops ?? 0))
      cur.failed += clampNonNeg(Number(t?.failed_ops ?? 0))
      cur.bytes += clampNonNeg(Number(t?.size_bytes ?? 0))
      cur.ms += clampNonNeg(Number(t?.duration_ms ?? 0))
      byMetric.set(k, cur)
    }

    const throughputPie = Array.from(byMetric.entries())
      .map(([k, v]) => ({ name: metricLabel(k), value: v.bytes }))
      .sort((a, b) => (b.value as number) - (a.value as number))

    const topMetric = throughputPie[0]?.name ?? '—'

    return {
      status: eventData.status,
      trigger: eventData.trigger,
      retry_attempt: clampNonNeg(eventData.retry_attempt ?? 0),
      beginMs,
      endMs,
      hbMs,
      durationMs,
      opsTotal,
      opsFailed,
      opsOk,
      bytesUp,
      bytesDown,
      numConflicts,
      unresolved,
      divergence,
      hashMismatch,
      localHash,
      remoteHash,
      configHash,
      stall_reason: eventData.stall_reason ?? null,
      error_code: eventData.error_code ?? null,
      error_message: eventData.error_message ?? null,
      throughputPie,
      topMetric,
      run_uuid: eventData.run_uuid ?? '',
      vault_id: eventData.vault_id ?? 0,
      now: Date.now(),
    }
  }, [event])

  useEffect(() => {
    if (!chartsRef.current || !d) return

    if (!chartRef.current) chartRef.current = echarts.init(chartsRef.current, undefined, { renderer: 'canvas' })
    const chart = chartRef.current

    const maxBytes = Math.max(d.bytesUp, d.bytesDown, 1)
    const hasThroughput = d.throughputPie.some(x => Number(x.value) > 0)

    const option: echarts.EChartsCoreOption = {
      grid: [
        { left: 10, right: 10, top: 24, height: 84, containLabel: true }, // ops bar
        { left: 10, right: 10, top: 130, height: 84, containLabel: true }, // up/down
      ],
      tooltip: {
        trigger: 'item',
        backgroundColor: 'rgba(10,10,12,0.85)',
        borderColor: 'rgba(255,255,255,0.10)',
        textStyle: { color: 'rgba(255,255,255,0.92)' },
      },
      xAxis: [
        {
          type: 'value',
          gridIndex: 0,
          max: d.opsTotal > 0 ? d.opsTotal : undefined,
          axisLabel: { color: 'rgba(255,255,255,0.45)' },
          splitLine: { show: true, lineStyle: { opacity: 0.18 } },
          axisLine: { lineStyle: { color: 'rgba(255,255,255,0.12)' } },
        },
        {
          type: 'value',
          gridIndex: 1,
          max: maxBytes,
          axisLabel: { formatter: (value: unknown) => bytes(Number(value)), color: 'rgba(255,255,255,0.45)' },
          splitLine: { show: true, lineStyle: { opacity: 0.18 } },
          axisLine: { lineStyle: { color: 'rgba(255,255,255,0.12)' } },
        },
      ],
      yAxis: [
        {
          type: 'category',
          gridIndex: 0,
          data: ['Ops'],
          axisLabel: { show: false },
          axisTick: { show: false },
          axisLine: { show: false },
        },
        {
          type: 'category',
          gridIndex: 1,
          data: ['Traffic'],
          axisLabel: { show: false },
          axisTick: { show: false },
          axisLine: { show: false },
        },
      ],
      series: [
        // Ops bar
        {
          name: 'OK',
          type: 'bar',
          stack: 'ops',
          xAxisIndex: 0,
          yAxisIndex: 0,
          barWidth: 18,
          data: [d.opsOk],
          itemStyle: { borderRadius: [10, 0, 0, 10], opacity: 0.92 },
          tooltip: { valueFormatter: (value: unknown) => `${Number(value).toLocaleString()} ops` },
        },
        {
          name: 'Failed',
          type: 'bar',
          stack: 'ops',
          xAxisIndex: 0,
          yAxisIndex: 0,
          barWidth: 18,
          data: [d.opsFailed],
          itemStyle: { borderRadius: [0, 10, 10, 0], opacity: 0.55 },
          tooltip: { valueFormatter: (value: unknown) => `${Number(value).toLocaleString()} ops` },
        },

        // Bytes up/down
        {
          name: 'Up',
          type: 'bar',
          stack: 'io',
          xAxisIndex: 1,
          yAxisIndex: 1,
          barWidth: 18,
          data: [d.bytesUp],
          itemStyle: { borderRadius: [10, 0, 0, 10], opacity: 0.9 },
          tooltip: { valueFormatter: (value: unknown) => bytes(Number(value)) },
        },
        {
          name: 'Down',
          type: 'bar',
          stack: 'io',
          xAxisIndex: 1,
          yAxisIndex: 1,
          barWidth: 18,
          data: [d.bytesDown],
          itemStyle: { borderRadius: [0, 10, 10, 0], opacity: 0.7 },
          tooltip: { valueFormatter: (value: unknown) => bytes(Number(value)) },
        },

        // Throughput pie (inset)
        {
          name: 'By metric',
          type: 'pie',
          radius: ['58%', '82%'],
          center: ['83%', '38%'],
          data: hasThroughput ? d.throughputPie : [{ name: 'No data', value: 1 }],
          label: { show: false },
          labelLine: { show: false },
          itemStyle: { borderRadius: 10, borderColor: 'rgba(0,0,0,0)', borderWidth: 2 },
          tooltip: {
            formatter: (param: unknown) => {
              const datum = (param ?? {}) as PieTooltipDatum
              return `${datum.name ?? ''}: ${bytes(Number(datum.value ?? 0))}`
            },
          },
        },
      ],
    }

    chart.setOption(option, true)

    const ro = new ResizeObserver(() => chart.resize())
    ro.observe(chartsRef.current)
    return () => ro.disconnect()
  }, [d])

  if (!event) {
    return (
      <StatsCard title={title} subtitle="No recent sync event recorded">
        <div className="rounded-3xl border border-white/10 bg-white/5 p-4 text-sm text-white/60 backdrop-blur">
          <div className="font-semibold text-white/80">No data</div>
          <div className="mt-1 text-[12px] text-white/45">
            Once the first sync runs, this panel will light up with health stats (ops, bytes, conflicts, heartbeat).
          </div>
        </div>
      </StatsCard>
    )
  }

  if (!d) return null

  const now = d.now
  const hbTone = heartbeatTone(d.hbMs, now)
  const hbText = d.hbMs != null ? relTimeFromNow(d.hbMs, now) : '—'
  const hbSub = d.hbMs != null ? `heartbeat` : 'no heartbeat'

  const statusSub =
    d.status === 'running' && d.beginMs != null ? `started ${relTimeFromNow(d.beginMs, now)}`
    : d.endMs != null ? `ended ${relTimeFromNow(d.endMs, now)}`
    : d.beginMs != null ? `started ${relTimeFromNow(d.beginMs, now)}`
    : '—'

  const duration = fmtMs(d.durationMs)

  const warnFlags = [
    d.divergence ? 'divergence' : null,
    d.hashMismatch ? 'hash mismatch' : null,
    d.numConflicts > 0 ? `${d.numConflicts} conflicts` : null,
    d.unresolved > 0 ? `${d.unresolved} unresolved` : null,
    d.status === 'stalled' ? 'stalled' : null,
    d.status === 'error' ? 'error' : null,
  ].filter(Boolean) as string[]

  const right = (
    <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
      <Pill label="Status" value={String(d.status)} sub={statusSub} tone={statusTone(d.status)} />
      <Pill
        label="Trigger"
        value={`${d.trigger}${d.retry_attempt ? ` • r${d.retry_attempt}` : ''}`}
        sub={d.run_uuid ? `run ${String(d.run_uuid).slice(0, 8)}…` : undefined}
      />
      <Pill
        label="Duration"
        value={duration}
        sub={d.endMs ? 'completed' : 'live'}
        tone={d.endMs ? 'neutral' : 'good'}
      />
      <Pill label="Heartbeat" value={hbText} sub={hbSub} tone={hbTone} />
    </div>
  )

  const subtitleParts = [
    `Ops ${d.opsOk.toLocaleString()}/${d.opsTotal.toLocaleString()} OK`,
    `Fail ${pct(d.opsFailed, d.opsTotal)}`,
    `Up ${bytes(d.bytesUp)}`,
    `Down ${bytes(d.bytesDown)}`,
    d.numConflicts ? `Conflicts ${d.numConflicts}` : null,
  ].filter(Boolean)

  const alertBlock =
    warnFlags.length || d.error_message || d.stall_reason ?
      <div className="mt-3 rounded-3xl border border-white/10 bg-white/5 p-3 backdrop-blur">
        <div className="flex flex-wrap items-center gap-2">
          {warnFlags.length ?
            <div className="text-xs font-semibold text-white/80">Flags</div>
          : <div className="text-xs font-semibold text-white/80">Status</div>}
          <div className="flex flex-wrap gap-2">
            {warnFlags.length ?
              warnFlags.map(x => (
                <span
                  key={x}
                  className="rounded-full border border-white/10 bg-white/5 px-2 py-1 text-[11px] text-white/65">
                  {x}
                </span>
              ))
            : <span className="text-[11px] text-white/45">No warnings</span>}
          </div>
        </div>

        {d.stall_reason || d.error_code || d.error_message ?
          <div className="mt-2 text-[12px] text-white/55">
            {d.stall_reason ?
              <div>
                <span className="font-semibold text-white/75">Stall:</span> {d.stall_reason}
              </div>
            : null}
            {d.error_code ?
              <div>
                <span className="font-semibold text-white/75">Error code:</span> {d.error_code}
              </div>
            : null}
            {d.error_message ?
              <div className="mt-1 text-white/60">
                <span className="font-semibold text-white/75">Message:</span> {d.error_message}
              </div>
            : null}
          </div>
        : null}
      </div>
    : null

  return (
    <StatsCard title={title} subtitle={subtitleParts.join(' • ')} right={right}>
      <div className="grid grid-cols-1 gap-4 lg:grid-cols-3">
        <div className="rounded-3xl border border-white/10 bg-white/5 p-3 backdrop-blur lg:col-span-2">
          <div className="mb-2 flex items-center justify-between">
            <div className="text-xs font-semibold text-white/80">Ops + traffic</div>
            <div className="text-[11px] text-white/45">
              Failed {d.opsFailed.toLocaleString()} • Conflicts {d.numConflicts.toLocaleString()} • Top {d.topMetric}
            </div>
          </div>

          <div ref={chartsRef} className="h-55 w-full" />

          <div className="mt-2 flex flex-wrap gap-2 text-[11px] text-white/45">
            <span>
              Vault: <span className="font-semibold text-white/75">{Number(d.vault_id).toLocaleString()}</span>
            </span>
            <span>•</span>
            <span>
              Divergence:{' '}
              <span className={`font-semibold ${d.divergence ? 'text-amber-300' : 'text-white/75'}`}>
                {d.divergence ? 'detected' : 'no'}
              </span>
            </span>
            <span>•</span>
            <span>
              Hash:{' '}
              <span className={`font-semibold ${d.hashMismatch ? 'text-amber-300' : 'text-white/75'}`}>
                {d.hashMismatch ? 'mismatch' : 'ok'}
              </span>
            </span>
          </div>

          {alertBlock}
        </div>

        <div className="rounded-3xl border border-white/10 bg-white/5 p-3 backdrop-blur">
          <div className="text-xs font-semibold text-white/80">Quick read</div>

          <div className="mt-2 grid grid-cols-1 gap-2">
            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2">
              <div className="text-[11px] text-white/55">Success rate</div>
              <div className="text-sm font-semibold text-white/90">{pct(d.opsOk, d.opsTotal)}</div>
            </div>

            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2">
              <div className="text-[11px] text-white/55">Unresolved conflicts</div>
              <div className="text-sm font-semibold text-white/90">{d.unresolved.toLocaleString()}</div>
            </div>

            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2">
              <div className="text-[11px] text-white/55">Traffic</div>
              <div className="text-sm font-semibold text-white/90">{bytes(d.bytesUp + d.bytesDown)}</div>
              <div className="text-[11px] text-white/45">
                Up {bytes(d.bytesUp)} • Down {bytes(d.bytesDown)}
              </div>
            </div>

            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2">
              <div className="text-[11px] text-white/55">Run UUID</div>
              <div className="text-[12px] font-semibold break-all text-white/85">{String(d.run_uuid || '—')}</div>
            </div>
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
