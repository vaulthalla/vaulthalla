'use client'

import React, { useId, useMemo, useState } from 'react'
import { cn } from '@/util/cn'
import { toneClasses, type Tone } from '@/lib/tone'
import { formatBytes, formatCompact, formatDateTime, formatDuration, formatPercent, parseDate } from '@/lib/format'
import type { TrendPoint } from '@/features/vaults/overview/types'

// Small SVG charts for the vault overview. No chart library: a line/area with a hover readout is all it needs.

export const formatUnit = (value: number | null | undefined, unit?: string) => {
  if (value === null || value === undefined || !Number.isFinite(value)) return 'not available'
  switch (unit) {
    case 'bytes':
      return formatBytes(value)
    case 'ratio':
      return formatPercent(value, { digits: 0 })
    case 'seconds':
      return formatDuration(value)
    default:
      return formatCompact(value)
  }
}

interface Pt {
  t: number
  v: number | null
}

const W = 320
const H = 96
const PAD_TOP = 8
const PAD_BOTTOM = 4

// One series over time. Gaps (null) break the line; a series with fewer than two known points says so instead of
// drawing a flat line that would look like "nothing happened".
export const TrendChart = ({
  points,
  unit,
  label,
  tone = 'accent',
  className,
}: {
  points: TrendPoint[]
  unit?: string
  label: string
  tone?: Tone
  className?: string
}) => {
  const gradientId = useId()
  const [hover, setHover] = useState<number | null>(null)

  const data = useMemo<Pt[]>(
    () =>
      points
        .map(p => ({ t: parseDate(p.created_at)?.getTime() ?? NaN, v: typeof p.value === 'number' && Number.isFinite(p.value) ? p.value : null }))
        .filter(p => Number.isFinite(p.t))
        .sort((a, b) => a.t - b.t),
    [points],
  )
  const known = data.filter((p): p is { t: number; v: number } => p.v !== null)

  if (known.length < 2)
    return (
      <div className={cn('grid h-24 place-items-center rounded-control border border-dashed border-line text-xs text-fg-faint', className)}>
        {known.length === 1 ? `One sample: ${formatUnit(known[0].v, unit)}` : 'No samples in this window'}
      </div>
    )

  const t0 = data[0].t
  const t1 = data[data.length - 1].t
  const span = t1 - t0 || 1
  const low = Math.min(...known.map(p => p.v))
  const high = Math.max(...known.map(p => p.v))
  let min = low
  let max = high
  if (unit === 'ratio') {
    min = Math.min(0, min)
    max = Math.max(1, max)
  }
  // A flat series sits low in the plot (on the floor when it is zero) instead of floating mid-air.
  if (min === max) {
    if (min >= 0) {
      min = 0
      max = max === 0 ? 1 : max / 0.7
    } else {
      max = 0
      min = min / 0.7
    }
  }
  const x = (t: number) => ((t - t0) / span) * W
  const y = (v: number) => PAD_TOP + (1 - (v - min) / (max - min)) * (H - PAD_TOP - PAD_BOTTOM)

  // Split into runs of known values so nulls render as gaps.
  const runs: { t: number; v: number }[][] = []
  let run: { t: number; v: number }[] = []
  for (const p of data) {
    if (p.v === null) {
      if (run.length) runs.push(run)
      run = []
    } else run.push({ t: p.t, v: p.v })
  }
  if (run.length) runs.push(run)

  const line = runs.map(r => r.map((p, i) => `${i ? 'L' : 'M'}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`).join(' ')).join(' ')
  const area = runs
    .filter(r => r.length > 1)
    .map(r => `M${x(r[0].t).toFixed(1)},${H} ${r.map(p => `L${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`).join(' ')} L${x(r[r.length - 1].t).toFixed(1)},${H} Z`)
    .join(' ')

  const latest = known[known.length - 1]
  const active = hover !== null ? data[hover] : null

  const onMove = (event: React.PointerEvent<SVGSVGElement>) => {
    const rect = event.currentTarget.getBoundingClientRect()
    const t = t0 + ((event.clientX - rect.left) / rect.width) * span
    let best = 0
    for (let i = 1; i < data.length; i++) if (Math.abs(data[i].t - t) < Math.abs(data[best].t - t)) best = i
    setHover(best)
  }

  return (
    <figure className={cn('min-w-0', className)}>
      <div className="relative">
        <svg
          viewBox={`0 0 ${W} ${H}`}
          preserveAspectRatio="none"
          className={cn('h-24 w-full overflow-visible', toneClasses[tone].text)}
          role="img"
          aria-label={`${label}: latest ${formatUnit(latest.v, unit)}, range ${formatUnit(low, unit)} to ${formatUnit(high, unit)}`}
          onPointerMove={onMove}
          onPointerLeave={() => setHover(null)}>
          <defs>
            <linearGradient id={gradientId} x1="0" x2="0" y1="0" y2="1">
              <stop offset="0%" stopColor="currentColor" stopOpacity="0.22" />
              <stop offset="100%" stopColor="currentColor" stopOpacity="0" />
            </linearGradient>
          </defs>
          {[0.25, 0.5, 0.75].map(f => (
            <line key={f} x1="0" x2={W} y1={PAD_TOP + f * (H - PAD_TOP - PAD_BOTTOM)} y2={PAD_TOP + f * (H - PAD_TOP - PAD_BOTTOM)} className="stroke-line" strokeWidth="1" vectorEffect="non-scaling-stroke" />
          ))}
          <path d={area} fill={`url(#${gradientId})`} />
          <path d={line} fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinejoin="round" strokeLinecap="round" vectorEffect="non-scaling-stroke" />
          {active ? (
            <line x1={x(active.t)} x2={x(active.t)} y1="0" y2={H} className="stroke-line-strong" strokeWidth="1" vectorEffect="non-scaling-stroke" />
          ) : null}
        </svg>
        {active && active.v !== null ? (
          <span
            className="pointer-events-none absolute size-2 -translate-x-1/2 -translate-y-1/2 rounded-full bg-fg ring-2 ring-bg"
            style={{ left: `${(x(active.t) / W) * 100}%`, top: `${(y(active.v) / H) * 100}%` }}
          />
        ) : null}
      </div>
      <figcaption className="mt-1.5 flex items-baseline justify-between gap-2 text-xs tabular">
        <span className="truncate text-fg-subtle">
          {active ? formatDateTime(active.t) : low === high ? `flat at ${formatUnit(low, unit)}` : `${formatUnit(low, unit)} – ${formatUnit(high, unit)}`}
        </span>
        <span className="text-fg">{active ? formatUnit(active.v, unit) : formatUnit(latest.v, unit)}</span>
      </figcaption>
    </figure>
  )
}

// Decorative category colours (never status colours): file types and the like.
const CATEGORY = ['bg-accent', 'bg-violet', 'bg-pink', 'bg-accent/45', 'bg-violet/45', 'bg-fg-faint']

export const ProportionBar = ({
  parts,
  className,
}: {
  parts: { label: string; value: number; display: string }[]
  className?: string
}) => {
  const sum = parts.reduce((acc, p) => acc + p.value, 0)
  return (
    <div className={className}>
      <div className="flex h-2 gap-px overflow-hidden rounded-full bg-surface-3">
        {sum > 0
          ? parts.map((p, i) => (
              <div key={p.label} className={cn('h-full', CATEGORY[Math.min(i, CATEGORY.length - 1)])} style={{ width: `${(p.value / sum) * 100}%` }} />
            ))
          : null}
      </div>
      <dl className="mt-2.5 flex flex-wrap gap-x-5 gap-y-1.5 text-xs">
        {parts.map((p, i) => (
          <div key={p.label} className="flex items-center gap-1.5">
            <span className={cn('size-2 rounded-full', CATEGORY[Math.min(i, CATEGORY.length - 1)])} aria-hidden />
            <dt className="font-mono text-fg-muted">{p.label}</dt>
            <dd className="text-fg-subtle tabular">{p.display}</dd>
          </div>
        ))}
      </dl>
    </div>
  )
}

// Two-part proportion (e.g. upload vs download) with exact numbers beside it.
export const SplitBar = ({
  parts,
  format,
  className,
}: {
  parts: { label: string; value: number | null | undefined; tone: Tone }[]
  format: (value: number | null | undefined) => string | null
  className?: string
}) => {
  const known = parts.filter(p => typeof p.value === 'number' && (p.value as number) > 0)
  const sum = known.reduce((acc, p) => acc + (p.value as number), 0)
  return (
    <div className={className}>
      <div className="flex h-1.5 overflow-hidden rounded-full bg-surface-3">
        {sum > 0
          ? known.map(p => <div key={p.label} className={cn('h-full', toneClasses[p.tone].dot)} style={{ width: `${((p.value as number) / sum) * 100}%` }} />)
          : null}
      </div>
      <dl className="mt-2 flex flex-wrap gap-x-5 gap-y-1 text-xs">
        {parts.map(p => (
          <div key={p.label} className="flex items-center gap-1.5">
            <span className={cn('size-2 rounded-full', toneClasses[p.tone].dot)} aria-hidden />
            <dt className="text-fg-subtle">{p.label}</dt>
            <dd className={cn('tabular', format(p.value) === null ? 'text-fg-faint' : 'text-fg')}>{format(p.value) ?? 'not available'}</dd>
          </div>
        ))}
      </dl>
    </div>
  )
}
