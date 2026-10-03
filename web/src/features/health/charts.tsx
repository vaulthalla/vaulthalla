'use client'

// Small SVG charts for the health pages. No chart library: a time-series line/area chart with a crosshair tooltip,
// a donut and a horizontal bar list. Series colors are decorative (accent → violet → pink, in that fixed order)
// and never status colors.

import React, { useCallback, useEffect, useId, useMemo, useRef, useState } from 'react'
import { cn } from '@/util/cn'
import { toneClasses, type Tone } from '@/lib/tone'
import { formatBytes, formatCompact, formatDateTime, formatDuration, formatPercent, formatTime } from '@/lib/format'
import type { SeriesPoint } from '@/features/health/model'

export const SERIES_COLORS = ['text-accent', 'text-violet', 'text-pink'] as const
const SERIES_DOTS = ['bg-accent', 'bg-violet', 'bg-pink'] as const
const STACK_DOTS = ['bg-accent', 'bg-violet', 'bg-pink', 'bg-fg-subtle'] as const
const STACK_TEXT = ['text-accent', 'text-violet', 'text-pink', 'text-fg-subtle'] as const

export const formatUnit = (value: number | null | undefined, unit: string | null | undefined): string => {
  if (value === null || value === undefined || !Number.isFinite(value)) return '—'
  switch (unit) {
    case 'ratio':
      return formatPercent(value, { digits: value !== 0 && Math.abs(value) < 0.01 ? 2 : 1 })
    case 'bytes':
      return formatBytes(value)
    case 'bytes/s':
      return `${formatBytes(value)}/s`
    case 'ms':
      return `${value < 10 ? value.toFixed(1) : Math.round(value)} ms`
    case 'seconds':
      return formatDuration(value)
    case 'ops/s':
      return `${value < 10 ? value.toFixed(2) : formatCompact(value)}/s`
    default:
      return Number.isInteger(value) ? formatCompact(value) : value.toFixed(2)
  }
}

// Tick labels: fewer decimals than tooltips.
const axisLabel = (value: number, unit: string, max: number) => {
  if (unit === 'ratio') return formatPercent(value, { digits: max < 0.1 ? 1 : 0 })
  if (unit === 'ops/s') return `${value < 10 ? value.toFixed(1) : formatCompact(value)}/s`
  if (unit === 'count') return formatCompact(value)
  return formatUnit(value, unit)
}

const useWidth = <T extends HTMLElement>() => {
  const ref = useRef<T>(null)
  const [width, setWidth] = useState(0)
  useEffect(() => {
    const el = ref.current
    if (!el) return
    const observer = new ResizeObserver(entries => setWidth(Math.floor(entries[0].contentRect.width)))
    observer.observe(el)
    return () => observer.disconnect()
  }, [])
  return [ref, width] as const
}

// Nice-ish y ticks: 0 (or the min when negative), mid, max rounded up.
const niceMax = (max: number, unit: string) => {
  if (unit === 'ratio') return max <= 0 ? 1 : max > 1 ? niceMaxRaw(max) : Math.min(1, Math.max(max * 1.15, 0.0001))
  // Counts get integer ticks (0, 1, 2 at least).
  if (unit === 'count') return Math.max(2, Math.ceil(niceMaxRaw(max)))
  return niceMaxRaw(max)
}

const niceMaxRaw = (max: number) => {
  if (max <= 0) return 1
  const mag = 10 ** Math.floor(Math.log10(max))
  const steps = [1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10]
  return (steps.find(s => s * mag >= max * 1.05) ?? 10) * mag
}

export interface ChartSeries {
  key: string
  label: string
  points: SeriesPoint[]
}

// Line chart over time; one area-filled series or up to three lines (more are cut, never recolored). Hover or
// focus shows a crosshair with every series' value at the nearest sample.
export const TimeSeriesChart = ({
  series,
  unit,
  height = 160,
  compact = false,
  className,
  label,
}: {
  series: ChartSeries[]
  unit: string
  height?: number
  compact?: boolean
  className?: string
  label: string
}) => {
  const [ref, width] = useWidth<HTMLDivElement>()
  const clipId = useId()
  const [hover, setHover] = useState<number | null>(null)
  const shown = useMemo(() => series.filter(s => s.points.length > 0).slice(0, SERIES_COLORS.length), [series])

  const geometry = useMemo(() => {
    const all = shown.flatMap(s => s.points)
    if (all.length < 2 || width <= 0) return null
    const pad = compact ? { l: 2, r: 2, t: 4, b: 2 } : { l: 52, r: 8, t: 8, b: 22 }
    const t0 = Math.min(...all.map(p => p.t))
    const t1 = Math.max(...all.map(p => p.t))
    const vmin = Math.min(0, ...all.map(p => p.v))
    const vmax = niceMax(Math.max(...all.map(p => p.v)), unit)
    const w = Math.max(1, width - pad.l - pad.r)
    const h = Math.max(1, height - pad.t - pad.b)
    const x = (t: number) => pad.l + (t1 === t0 ? w / 2 : ((t - t0) / (t1 - t0)) * w)
    const y = (v: number) => pad.t + h - ((v - vmin) / (vmax - vmin || 1)) * h
    // Union of sample times, for the crosshair.
    const times = [...new Set(all.map(p => p.t))].sort((a, b) => a - b)
    return { pad, t0, t1, vmin, vmax, w, h, x, y, times }
  }, [shown, width, height, unit, compact])

  const onMove = useCallback(
    (event: React.PointerEvent<SVGSVGElement>) => {
      if (!geometry) return
      const rect = event.currentTarget.getBoundingClientRect()
      const px = event.clientX - rect.left
      let best = 0
      let bestDist = Infinity
      geometry.times.forEach((t, i) => {
        const d = Math.abs(geometry.x(t) - px)
        if (d < bestDist) {
          bestDist = d
          best = i
        }
      })
      setHover(best)
    },
    [geometry],
  )

  const onKey = (event: React.KeyboardEvent) => {
    if (!geometry) return
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      event.preventDefault()
      const last = geometry.times.length - 1
      setHover(h => Math.max(0, Math.min(last, (h ?? last) + (event.key === 'ArrowLeft' ? -1 : 1))))
    }
  }

  const hoverT = geometry && hover !== null ? geometry.times[hover] : null
  const nearest = (s: ChartSeries, t: number) => {
    let best: SeriesPoint | null = null
    for (const p of s.points) if (!best || Math.abs(p.t - t) < Math.abs(best.t - t)) best = p
    return best
  }

  return (
    <div ref={ref} className={cn('relative min-w-0', className)} style={{ height }}>
      {geometry ? (
        <svg
          width={width}
          height={height}
          role="img"
          aria-label={label}
          tabIndex={compact ? -1 : 0}
          className="block overflow-visible focus:outline-none focus-visible:outline-2 focus-visible:outline-accent"
          onPointerMove={onMove}
          onPointerLeave={() => setHover(null)}
          onFocus={() => setHover(geometry.times.length - 1)}
          onBlur={() => setHover(null)}
          onKeyDown={onKey}>
          <defs>
            <clipPath id={clipId}>
              <rect x={geometry.pad.l} y={0} width={geometry.w} height={geometry.pad.t + geometry.h + 1} />
            </clipPath>
          </defs>
          {!compact ? (
            <g className="text-fg-faint" fontSize={10}>
              {[geometry.vmin, (geometry.vmin + geometry.vmax) / 2, geometry.vmax].map((v, i) => (
                <g key={i}>
                  <line
                    x1={geometry.pad.l}
                    x2={geometry.pad.l + geometry.w}
                    y1={geometry.y(v)}
                    y2={geometry.y(v)}
                    stroke="currentColor"
                    strokeOpacity={i === 0 ? 0.5 : 0.2}
                    strokeDasharray={i === 0 ? undefined : '2 4'}
                  />
                  <text x={geometry.pad.l - 6} y={geometry.y(v)} dy="0.32em" textAnchor="end" fill="currentColor" className="tabular">
                    {axisLabel(v, unit, geometry.vmax)}
                  </text>
                </g>
              ))}
              {[geometry.t0, geometry.t1].map((t, i) => (
                <text
                  key={i}
                  x={i === 0 ? geometry.pad.l : geometry.pad.l + geometry.w}
                  y={height - 6}
                  textAnchor={i === 0 ? 'start' : 'end'}
                  fill="currentColor"
                  className="tabular">
                  {geometry.t1 - geometry.t0 > 86_400 ? formatDateTime(t) : formatTime(t)}
                </text>
              ))}
            </g>
          ) : null}
          <g clipPath={`url(#${clipId})`}>
            {shown.map((s, i) => {
              const pts = s.points.map(p => [geometry.x(p.t), geometry.y(p.v)] as const)
              const line = pts.map(([x, y], j) => `${j ? 'L' : 'M'}${x.toFixed(1)},${y.toFixed(1)}`).join(' ')
              const base = geometry.y(Math.max(0, geometry.vmin))
              const area =
                pts.length > 1 ? `${line} L${pts[pts.length - 1][0].toFixed(1)},${base} L${pts[0][0].toFixed(1)},${base} Z` : ''
              return (
                <g key={s.key} className={SERIES_COLORS[i]}>
                  {shown.length === 1 && area ? <path d={area} fill="currentColor" opacity={0.1} /> : null}
                  {pts.length > 1 ? (
                    <path d={line} fill="none" stroke="currentColor" strokeWidth={compact ? 1.5 : 2} strokeLinejoin="round" strokeLinecap="round" />
                  ) : (
                    <circle cx={pts[0][0]} cy={pts[0][1]} r={3} fill="currentColor" />
                  )}
                </g>
              )
            })}
          </g>
          {hoverT !== null ? (
            <g>
              <line
                x1={geometry.x(hoverT)}
                x2={geometry.x(hoverT)}
                y1={geometry.pad.t}
                y2={geometry.pad.t + geometry.h}
                className="text-fg-subtle"
                stroke="currentColor"
                strokeWidth={1}
              />
              {shown.map((s, i) => {
                const p = nearest(s, hoverT)
                return p ? (
                  <circle
                    key={s.key}
                    cx={geometry.x(p.t)}
                    cy={geometry.y(p.v)}
                    r={4}
                    className={cn(SERIES_COLORS[i], 'stroke-bg')}
                    fill="currentColor"
                    strokeWidth={2}
                  />
                ) : null
              })}
            </g>
          ) : null}
        </svg>
      ) : (
        <div className="grid h-full place-items-center rounded-control border border-dashed border-line text-xs text-fg-faint">
          {shown.some(s => s.points.length === 1) ? 'One sample so far' : 'No samples yet'}
        </div>
      )}
      {geometry && hoverT !== null ? (
        <div
          role="status"
          className="pointer-events-none absolute top-0 z-10 min-w-36 rounded-control glass-strong px-2.5 py-2 text-xs shadow-pop"
          style={geometry.x(hoverT) > width / 2 ? { right: width - geometry.x(hoverT) + 10 } : { left: geometry.x(hoverT) + 10 }}>
          <div className="mb-1 text-fg-subtle tabular">{formatDateTime(hoverT)}</div>
          {shown.map((s, i) => {
            const p = nearest(s, hoverT)
            return (
              <div key={s.key} className="flex items-center justify-between gap-3">
                <span className="inline-flex min-w-0 items-center gap-1.5 text-fg-muted">
                  <span className={cn('size-2 shrink-0 rounded-full', SERIES_DOTS[i])} />
                  <span className="truncate">{s.label}</span>
                </span>
                <span className="font-medium text-fg tabular">{formatUnit(p?.v, unit)}</span>
              </div>
            )
          })}
        </div>
      ) : null}
    </div>
  )
}

export const SeriesLegend = ({ series, className }: { series: { key: string; label: string }[]; className?: string }) =>
  series.length > 1 ? (
    <ul className={cn('flex flex-wrap gap-x-3 gap-y-1 text-xs text-fg-subtle', className)}>
      {series.slice(0, SERIES_COLORS.length).map((s, i) => (
        <li key={s.key} className="inline-flex items-center gap-1.5">
          <span className={cn('h-0.5 w-3 rounded-full', SERIES_DOTS[i])} />
          {s.label}
        </li>
      ))}
    </ul>
  ) : null

export interface DonutDatum {
  label: string
  value: number | null
  tone?: Tone
  className?: string
}

// Share of a whole. Unknown values are listed as "not available" and never drawn; an all-zero whole draws an
// empty ring.
export const Donut = ({
  data,
  center,
  caption,
  size = 112,
  format = (v: number) => formatCompact(v),
  label,
}: {
  data: DonutDatum[]
  center: React.ReactNode
  caption?: React.ReactNode
  size?: number
  format?: (v: number) => string
  label: string
}) => {
  const known = data.filter(d => typeof d.value === 'number' && d.value > 0)
  const total = known.reduce((sum, d) => sum + (d.value as number), 0)
  const r = size / 2 - 7
  const c = 2 * Math.PI * r
  let offset = 0
  const gap = known.length > 1 ? 2 : 0
  return (
    <div className="flex items-center gap-4">
      <div className="relative shrink-0" style={{ width: size, height: size }}>
        <svg width={size} height={size} role="img" aria-label={label} className="-rotate-90">
          <circle cx={size / 2} cy={size / 2} r={r} fill="none" strokeWidth={10} className="stroke-surface-3" />
          {total > 0
            ? known.map(d => {
                const len = ((d.value as number) / total) * c
                const dash = Math.max(0, len - gap)
                const el = (
                  <circle
                    key={d.label}
                    cx={size / 2}
                    cy={size / 2}
                    r={r}
                    fill="none"
                    strokeWidth={10}
                    stroke="currentColor"
                    className={d.className ?? (d.tone ? toneClasses[d.tone].text : STACK_TEXT[data.indexOf(d) % STACK_TEXT.length])}
                    strokeDasharray={`${dash} ${c - dash}`}
                    strokeDashoffset={-offset}
                  />
                )
                offset += len
                return el
              })
            : null}
        </svg>
        <div className="absolute inset-0 grid place-items-center text-center">
          <div>
            <div className="text-base font-semibold text-fg tabular">{center}</div>
            {caption ? <div className="text-[11px] text-fg-subtle">{caption}</div> : null}
          </div>
        </div>
      </div>
      <ul className="min-w-0 space-y-1.5 text-sm">
        {data.map((d, i) => (
          <li key={d.label} className="flex items-center gap-2">
            <span
              className={cn(
                'size-2 shrink-0 rounded-full',
                d.className ? d.className.replace('text-', 'bg-') : d.tone ? toneClasses[d.tone].dot : STACK_DOTS[i % STACK_DOTS.length],
              )}
            />
            <span className="text-fg-muted">{d.label}</span>
            <span className="ml-auto pl-3 font-medium text-fg tabular">
              {typeof d.value === 'number' ? format(d.value) : <span className="font-normal text-fg-faint">not available</span>}
            </span>
          </li>
        ))}
      </ul>
    </div>
  )
}

export interface StackDatum {
  key: string
  label: string
  value: number | null
}

// Parts of a count (session mix, backend types) in one bar with a valued legend. Unknown parts are listed as
// "not available"; an all-zero total draws an empty track, never a "clear" color.
export const StackBar = ({ data, className }: { data: StackDatum[]; className?: string }) => {
  const total = data.reduce((sum, d) => sum + (typeof d.value === 'number' && d.value > 0 ? d.value : 0), 0)
  return (
    <div className={className}>
      <div
        className="flex h-2 w-full gap-0.5 overflow-hidden rounded-full bg-surface-3"
        role="img"
        aria-label={data.map(d => `${d.label} ${d.value ?? 'not available'}`).join(', ')}>
        {total > 0
          ? data.map((d, i) =>
              typeof d.value === 'number' && d.value > 0 ? (
                <div
                  key={d.key}
                  className={cn('h-full first:rounded-l-full last:rounded-r-full', STACK_DOTS[i % STACK_DOTS.length])}
                  style={{ width: `${(d.value / total) * 100}%` }}
                />
              ) : null,
            )
          : null}
      </div>
      <ul className="mt-2 flex flex-wrap gap-x-3.5 gap-y-1 text-xs">
        {data.map((d, i) => (
          <li key={d.key} className="inline-flex items-center gap-1.5 text-fg-subtle">
            <span className={cn('size-2 rounded-full', STACK_DOTS[i % STACK_DOTS.length])} aria-hidden />
            {d.label}
            <span className={cn('tabular', typeof d.value === 'number' ? 'font-medium text-fg' : 'text-fg-faint')}>
              {typeof d.value === 'number' ? formatCompact(d.value) : 'not available'}
            </span>
          </li>
        ))}
      </ul>
    </div>
  )
}

export interface BarItem {
  key: string
  label: React.ReactNode
  value: number | null
  display: React.ReactNode
  tone?: Tone
  hint?: React.ReactNode
}

// Ranked horizontal bars (largest tables, busiest FUSE ops). Bars scale to the largest known value.
export const BarList = ({ items, className, emptyLabel = 'Nothing to show' }: { items: BarItem[]; className?: string; emptyLabel?: string }) => {
  const max = Math.max(0, ...items.map(i => (typeof i.value === 'number' ? i.value : 0)))
  if (!items.length) return <p className="py-4 text-center text-sm text-fg-faint">{emptyLabel}</p>
  return (
    <ul className={cn('space-y-2', className)}>
      {items.map(item => (
        <li key={item.key} className="group" title={typeof item.hint === 'string' ? item.hint : undefined}>
          <div className="flex items-baseline justify-between gap-3 text-sm">
            <span className="min-w-0 truncate text-fg-muted">{item.label}</span>
            <span className="shrink-0 font-medium text-fg tabular">{item.display}</span>
          </div>
          <div className="mt-1 h-1.5 overflow-hidden rounded-full bg-surface-2">
            {typeof item.value === 'number' && item.value > 0 && max > 0 ? (
              <div
                className={cn('h-full rounded-full', item.tone ? toneClasses[item.tone].dot : 'bg-accent/70')}
                style={{ width: `${Math.max(1.5, (item.value / max) * 100)}%` }}
              />
            ) : null}
          </div>
        </li>
      ))}
    </ul>
  )
}
