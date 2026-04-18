'use client'

import React, { useEffect, useMemo, useRef } from 'react'
import * as echarts from 'echarts/core'
import { BarChart, PieChart, ScatterChart } from 'echarts/charts'
import { GridComponent, TooltipComponent, LegendComponent, TitleComponent, GraphicComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'

import { StatsCard } from '@/components/stats/StatsCard'
import { useStatsStore } from '@/stores/statsStore'
import { CacheStats } from '@/models/stats/cacheStats'
import type { CacheStatsWrapper } from '@/stores/statsStore' // adjust import path if needed

echarts.use([
  BarChart,
  PieChart,
  ScatterChart,
  GridComponent,
  GraphicComponent,
  TooltipComponent,
  LegendComponent,
  TitleComponent,
  CanvasRenderer,
])

type CacheStatsSource = 'fs' | 'http'
type StatsState = ReturnType<typeof useStatsStore.getState>

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const k = 1024
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(k)), units.length - 1)
  const v = bytes / Math.pow(k, i)
  return `${v >= 10 ? v.toFixed(0) : v.toFixed(1)} ${units[i]}`
}

function formatInt(n: number): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function msFromUs(us: number): number {
  if (!Number.isFinite(us) || us <= 0) return 0
  return us / 1000
}

const StatPill = ({ label, value }: { label: string; value: React.ReactNode }) => (
  <div className="flex items-center justify-between gap-3 rounded-2xl border border-white/10 bg-white/5 px-3 py-2 backdrop-blur">
    <div className="text-[11px] tracking-wide text-white/50 uppercase">{label}</div>
    <div className="text-sm font-semibold text-white/90">{value}</div>
  </div>
)

function getSourceConfig(source: CacheStatsSource) {
  if (source === 'http') {
    return {
      title: 'HTTP Cache',
      subtitle: 'Preview & request-path performance',
      // store selectors
      selectWrapper: (s: StatsState): CacheStatsWrapper => s.httpCacheStats,
      startPolling: (s: StatsState) => s.startHttpCacheStatsPolling,
      stopPolling: (s: StatsState) => s.stopHttpCacheStatsPolling,
    }
  }

  // default fs
  return {
    title: 'FS Cache',
    subtitle: 'Hot-path performance & memory footprint',
    selectWrapper: (s: StatsState): CacheStatsWrapper => s.fsCacheStats,
    startPolling: (s: StatsState) => s.startFsCacheStatsPolling,
    stopPolling: (s: StatsState) => s.stopFsCacheStatsPolling,
  }
}

export default function CacheStatsComponent({
  source = 'fs',
  intervalMs = 7500,
}: {
  source?: CacheStatsSource
  intervalMs?: number
}) {
  const cfg = getSourceConfig(source)

  const wrapper = useStatsStore(cfg.selectWrapper)
  const startPolling = useStatsStore(cfg.startPolling)
  const stopPolling = useStatsStore(cfg.stopPolling)

  const loading = !!wrapper?.loading
  const lastUpdated = wrapper?.lastUpdated ?? null
  const error = wrapper?.error ?? null

  const pieRef = useRef<HTMLDivElement | null>(null)
  const barsRef = useRef<HTMLDivElement | null>(null)
  const pieChartRef = useRef<echarts.EChartsType | null>(null)
  const barsChartRef = useRef<echarts.EChartsType | null>(null)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const derived = useMemo(() => {
    const s = wrapper?.data ?? new CacheStats({})

    const hits = s.hits ?? 0
    const misses = s.misses ?? 0
    const denom = hits + misses
    const hitRate = denom > 0 ? hits / denom : 0

    const used = Math.max(0, s.used_bytes ?? 0)
    const cap = Math.max(0, s.capacity_bytes ?? 0)
    const free = cap > used ? cap - used : 0

    const opCount = Math.max(0, s.op?.count ?? 0)
    const avgOpMs = opCount > 0 ? msFromUs(s.op?.total_us ?? 0) / opCount : 0
    const maxOpMs = msFromUs(s.op?.max_us ?? 0)

    return { s, hits, misses, hitRate, used, cap, free, opCount, avgOpMs, maxOpMs }
  }, [wrapper?.data])

  // init charts once
  useEffect(() => {
    if (!pieRef.current || !barsRef.current) return
    if (!pieChartRef.current) pieChartRef.current = echarts.init(pieRef.current)
    if (!barsChartRef.current) barsChartRef.current = echarts.init(barsRef.current)

    const onResize = () => {
      pieChartRef.current?.resize()
      barsChartRef.current?.resize()
    }
    window.addEventListener('resize', onResize)
    return () => window.removeEventListener('resize', onResize)
  }, [])

  // update charts whenever derived changes
  useEffect(() => {
    const pie = pieChartRef.current
    const bars = barsChartRef.current
    if (!pie || !bars) return

    const { hits, misses, hitRate, used, free } = derived

    const baseTooltip = {
      trigger: 'item' as const,
      backgroundColor: 'rgba(12,12,14,0.92)',
      borderColor: 'rgba(255,255,255,0.10)',
      textStyle: { color: 'rgba(255,255,255,0.90)' },
      extraCssText: 'border-radius:14px; backdrop-filter: blur(10px);',
    }

    pie.setOption(
      {
        tooltip: baseTooltip,
        legend: { show: false },
        series: [
          {
            name: 'Cache',
            type: 'pie',
            radius: ['62%', '82%'],
            center: ['50%', '50%'],
            itemStyle: { borderColor: 'rgba(0,0,0,0)', borderWidth: 2 },
            label: {
              show: true,
              position: 'center',
              formatter: () => `${Math.round(hitRate * 100)}%`,
              fontSize: 20,
              fontWeight: 700,
              color: 'rgba(255,255,255,0.92)',
            },
            data: [
              { value: hits, name: 'Hits' },
              { value: misses, name: 'Misses' },
            ],
          },
        ],
        graphic: [
          {
            type: 'text',
            left: 'center',
            top: '62%',
            style: { text: 'hit rate', fill: 'rgba(255,255,255,0.55)', fontSize: 11, fontWeight: 600 },
          },
        ],
      },
      { notMerge: true },
    )

    bars.setOption(
      {
        tooltip: { ...baseTooltip, trigger: 'axis', axisPointer: { type: 'shadow' } },
        grid: { left: 12, right: 12, top: 12, bottom: 0, containLabel: true },
        xAxis: {
          type: 'category',
          axisTick: { show: false },
          axisLine: { show: false },
          axisLabel: { color: 'rgba(255,255,255,0.55)', fontSize: 11 },
          data: ['Memory', 'Ops'],
        },
        yAxis: {
          type: 'value',
          axisLine: { show: false },
          splitLine: { lineStyle: { color: 'rgba(255,255,255,0.08)' } },
          axisLabel: { color: 'rgba(255,255,255,0.45)', fontSize: 10 },
        },
        series: [
          { name: 'Used Bytes', type: 'bar', stack: 'mem', barWidth: 18, data: [used, 0] },
          {
            name: 'Free Bytes',
            type: 'bar',
            stack: 'mem',
            barWidth: 18,
            itemStyle: { opacity: 0.55 },
            data: [free, 0],
          },

          { name: 'Inserts', type: 'bar', stack: 'ops', barWidth: 18, data: [0, derived.s.inserts ?? 0] },
          {
            name: 'Evictions',
            type: 'bar',
            stack: 'ops',
            barWidth: 18,
            itemStyle: { opacity: 0.7 },
            data: [0, derived.s.evictions ?? 0],
          },
          {
            name: 'Invalidations',
            type: 'bar',
            stack: 'ops',
            barWidth: 18,
            itemStyle: { opacity: 0.55 },
            data: [0, derived.s.invalidations ?? 0],
          },
        ],
      },
      { notMerge: true },
    )
  }, [derived])

  useEffect(() => {
    return () => {
      pieChartRef.current?.dispose()
      barsChartRef.current?.dispose()
      pieChartRef.current = null
      barsChartRef.current = null
    }
  }, [])

  const right = (
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
        'updating…'
      : 'live'}
      {lastUpdated ?
        <span className="text-white/35">·</span>
      : null}
      {lastUpdated ?
        <span className="text-white/50">{new Date(lastUpdated).toLocaleTimeString()}</span>
      : null}
    </div>
  )

  return (
    <StatsCard title={cfg.title} subtitle={cfg.subtitle} right={right} className="w-full">
      <div className="space-y-3">
        {/* charts: stack on small, side-by-side on lg+ */}
        <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
          {/* Hit/Miss */}
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
            <div className="mb-2 flex items-center justify-between">
              <div className="text-xs font-semibold text-white/70">Hit / Miss</div>
              <div className="text-[11px] text-white/45">{formatInt(derived.hits + derived.misses)} req</div>
            </div>
            <div ref={pieRef} className="h-[190px] w-full" />
            <div className="mt-2 grid grid-cols-2 gap-2">
              <StatPill label="Hits" value={formatInt(derived.hits)} />
              <StatPill label="Misses" value={formatInt(derived.misses)} />
            </div>
          </div>

          {/* Memory & Churn */}
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
            <div className="mb-2 flex items-center justify-between">
              <div className="text-xs font-semibold text-white/70">Memory & Churn</div>
              <div className="text-[11px] text-white/45">
                {formatBytes(derived.used)} / {formatBytes(derived.cap)} used
              </div>
            </div>
            <div ref={barsRef} className="h-[190px] w-full" />
          </div>
        </div>

        {!!error ?
          <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs text-rose-200/90">
            {error}
          </div>
        : null}

        {/* pills: full width under charts */}
        <div className="grid grid-cols-2 gap-2 lg:grid-cols-4">
          <StatPill label="Hit Rate" value={`${Math.round(derived.hitRate * 100)}%`} />
          <StatPill label="Avg Miss Work" value={`${derived.avgOpMs.toFixed(2)} ms`} />
          <StatPill label="Max Miss Work" value={`${derived.maxOpMs.toFixed(2)} ms`} />
          <StatPill label="Reads" value={formatBytes(derived.s.bytes_read)} />
        </div>

        <div className="grid grid-cols-2 gap-2 lg:grid-cols-4">
          <StatPill label="Writes" value={formatBytes(derived.s.bytes_written)} />
          <StatPill label="Inserts" value={formatInt(derived.s.inserts)} />
          <StatPill label="Evictions" value={formatInt(derived.s.evictions)} />
          <StatPill label="Invalidations" value={formatInt(derived.s.invalidations)} />
        </div>
      </div>
    </StatsCard>
  )
}
