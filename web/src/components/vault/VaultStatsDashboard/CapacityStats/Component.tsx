import React, { useEffect, useMemo, useRef } from 'react'

import * as echarts from 'echarts/core'

import type { CapacityStats as CapacityStatsModel } from '@/models/stats/vaultStats'
import { StatsCard } from '@/components/stats/StatsCard'
type CapacityStatsProps = { capacityStats: CapacityStatsModel; title?: string }
type CapacityStatsCompat = CapacityStatsModel & {
  capacity_bytes?: number
  physical_size_bytes?: number
  logical_size_bytes?: number
  cache_size_bytes?: number
  free_space_bytes?: number
  average_file_size_bytes?: number
  largest_file_size_bytes?: number
}

type PieTooltipDatum = { name?: string; value?: number }

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

function pct(part: number, whole: number): string {
  const w = clampNonNeg(whole)
  if (w <= 0) return '—'
  return `${((clampNonNeg(part) / w) * 100).toFixed(1)}%`
}

const Pill = ({ label, value, sub }: { label: string; value: string; sub?: string }) => (
  <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2 backdrop-blur">
    <div className="text-[11px] text-white/55">{label}</div>
    <div className="text-sm leading-5 font-semibold text-white/90">{value}</div>
    {sub ?
      <div className="text-[11px] text-white/45">{sub}</div>
    : null}
  </div>
)

export default function CapacityStats({ capacityStats, title = 'Capacity' }: CapacityStatsProps) {
  const usageRef = useRef<HTMLDivElement | null>(null)
  const extRef = useRef<HTMLDivElement | null>(null)
  const usageChartRef = useRef<echarts.EChartsType | null>(null)
  const extChartRef = useRef<echarts.EChartsType | null>(null)

  const d = useMemo(() => {
    const stats = capacityStats as CapacityStatsCompat
    const capacity = clampNonNeg(Number(stats.capacity_bytes ?? stats.capacity ?? 0))
    const physical = clampNonNeg(Number(stats.physical_size_bytes ?? stats.physical_size ?? 0))
    const logical = clampNonNeg(Number(stats.logical_size_bytes ?? stats.logical_size ?? 0))
    const cache = clampNonNeg(Number(stats.cache_size_bytes ?? stats.cache_size ?? 0))
    const free = clampNonNeg(Number(stats.free_space_bytes ?? stats.free_space ?? 0))

    const total = capacity > 0 ? capacity : physical + free
    const used = Math.min(physical, total)
    const freeShown = Math.max(0, total - used)

    const cacheShown = Math.min(cache, used)
    const dataShown = Math.max(0, used - cacheShown)

    // ✅ Record<string, number> → top-N array
    const extensions = Object.entries(stats.top_file_extensions ?? {})
      .map(([ext, bytes]) => ({ name: ext || 'unknown', value: clampNonNeg(Number(bytes) || 0) }))
      .sort((a, b) => b.value - a.value)
      .slice(0, 8)

    return {
      capacity,
      physical,
      logical,
      cache,
      free,
      total,
      used,
      freeShown,
      cacheShown,
      dataShown,
      average_file_size: clampNonNeg(Number(stats.average_file_size_bytes ?? stats.average_file_size ?? 0)),
      largest_file_size: clampNonNeg(Number(stats.largest_file_size_bytes ?? stats.largest_file_size ?? 0)),
      file_count: clampNonNeg(stats.file_count ?? 0),
      directory_count: clampNonNeg(stats.directory_count ?? 0),
      extensions,
    }
  }, [capacityStats])

  useEffect(() => {
    if (!usageRef.current || !extRef.current) return

    if (!usageChartRef.current)
      usageChartRef.current = echarts.init(usageRef.current, undefined, { renderer: 'canvas' })
    if (!extChartRef.current) extChartRef.current = echarts.init(extRef.current, undefined, { renderer: 'canvas' })

    const usageChart = usageChartRef.current
    const extChart = extChartRef.current

    const usageOption: echarts.EChartsCoreOption = {
      grid: { left: 8, right: 10, top: 28, bottom: 10, containLabel: true },
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'shadow' },
        valueFormatter: (value: unknown) => bytes(Number(value)),
        backgroundColor: 'rgba(10,10,12,0.85)',
        borderColor: 'rgba(255,255,255,0.10)',
        textStyle: { color: 'rgba(255,255,255,0.92)' },
      },
      legend: {
        show: true,
        top: 0,
        right: 0,
        itemWidth: 10,
        itemHeight: 10,
        textStyle: { color: 'rgba(255,255,255,0.55)' },
      },
      xAxis: {
        type: 'value',
        max: d.total > 0 ? d.total : undefined,
        axisLabel: { formatter: (value: unknown) => bytes(Number(value)), color: 'rgba(255,255,255,0.45)' },
        splitLine: { show: true, lineStyle: { opacity: 0.18 } },
        axisLine: { lineStyle: { color: 'rgba(255,255,255,0.12)' } },
      },
      yAxis: {
        type: 'category',
        data: [''],
        axisTick: { show: false },
        axisLine: { show: false },
        axisLabel: { show: false },
      },
      series: [
        {
          name: 'Data (physical)',
          type: 'bar',
          stack: 'cap',
          barWidth: 22,
          data: [d.dataShown],
          itemStyle: { borderRadius: [12, 0, 0, 12], opacity: 0.95 },
        },
        {
          name: 'Cache (physical)',
          type: 'bar',
          stack: 'cap',
          barWidth: 22,
          data: [d.cacheShown],
          itemStyle: { opacity: 0.75 },
        },
        {
          name: 'Free',
          type: 'bar',
          stack: 'cap',
          barWidth: 22,
          data: [d.freeShown],
          itemStyle: { borderRadius: [0, 12, 12, 0], opacity: 0.45 },
        },
        {
          name: 'Logical size',
          type: 'scatter',
          symbol: 'rect',
          symbolSize: [3, 26],
          data: [[Math.min(d.logical, d.total), 0]],
          z: 10,
          itemStyle: { opacity: 0.9 },
          tooltip: {
            valueFormatter: (value: unknown) => bytes(Number(Array.isArray(value) ? value[0] : value)),
          },
        },
      ],
    }

    const extOption: echarts.EChartsCoreOption = {
      tooltip: {
        trigger: 'item',
        backgroundColor: 'rgba(10,10,12,0.85)',
        borderColor: 'rgba(255,255,255,0.10)',
        textStyle: { color: 'rgba(255,255,255,0.92)' },
        formatter: (param: unknown) => {
          const datum = (param ?? {}) as PieTooltipDatum
          return `${datum.name ?? ''}: ${bytes(Number(datum.value ?? 0))}`
        },
      },
      title: {
        text: 'Top extensions',
        left: 'center',
        top: 6,
        textStyle: { fontSize: 12, fontWeight: 600, color: 'rgba(255,255,255,0.75)' },
      },
      series: [
        {
          type: 'pie',
          radius: ['55%', '80%'],
          center: ['50%', '58%'],
          avoidLabelOverlap: true,
          itemStyle: { borderRadius: 10, borderColor: 'rgba(0,0,0,0)', borderWidth: 2 },
          label: { show: false },
          labelLine: { show: false },
          data: d.extensions.length ? d.extensions : [{ name: 'No data', value: 1 }],
        },
      ],
    }

    usageChart.setOption(usageOption, true)
    extChart.setOption(extOption, true)

    const ro = new ResizeObserver(() => {
      usageChart.resize()
      extChart.resize()
    })
    ro.observe(usageRef.current)
    ro.observe(extRef.current)

    return () => ro.disconnect()
  }, [d])

  const right = (
    <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
      <Pill label="Used" value={bytes(d.used)} sub={pct(d.used, d.total)} />
      <Pill label="Free" value={bytes(d.freeShown)} sub={pct(d.freeShown, d.total)} />
      <Pill label="Logical" value={bytes(d.logical)} sub="marker" />
      <Pill label="Cache" value={bytes(d.cache)} sub={pct(d.cache, d.used)} />
    </div>
  )

  return (
    <StatsCard
      title={title}
      subtitle={`Used ${bytes(d.used)} • Free ${bytes(d.freeShown)} • Total ${bytes(d.total)}`}
      right={right}>
      <div className="grid grid-cols-1 gap-4 lg:grid-cols-3">
        <div className="rounded-3xl border border-white/10 bg-white/5 p-3 backdrop-blur lg:col-span-2">
          <div className="mb-2 flex items-center justify-between">
            <div className="text-xs font-semibold text-white/80">Disk bar + logical marker</div>
            <div className="text-[11px] text-white/45">
              Largest {bytes(d.largest_file_size)} • Avg {bytes(d.average_file_size)}
            </div>
          </div>
          <div ref={usageRef} className="h-[170px] w-full" />
          <div className="mt-2 flex flex-wrap gap-2 text-[11px] text-white/45">
            <span>
              Files: <span className="font-semibold text-white/75">{d.file_count.toLocaleString()}</span>
            </span>
            <span>•</span>
            <span>
              Dirs: <span className="font-semibold text-white/75">{d.directory_count.toLocaleString()}</span>
            </span>
          </div>
        </div>

        <div className="rounded-3xl border border-white/10 bg-white/5 p-3 backdrop-blur">
          <div ref={extRef} className="h-[210px] w-full" />
        </div>
      </div>
    </StatsCard>
  )
}
