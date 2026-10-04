import React from 'react'
import { cn } from '@/util/cn'
import { toneClasses, type Tone } from '@/lib/tone'
import { DASH } from '@/lib/format'

// A labelled value. `value` null/undefined renders "not available" — never 0, never a healthy tone.
export const StatTile = ({
  label,
  value,
  hint,
  tone,
  className,
  icon: Icon,
}: {
  label: React.ReactNode
  value: React.ReactNode | null | undefined
  hint?: React.ReactNode
  tone?: Tone
  className?: string
  icon?: React.ComponentType<React.SVGProps<SVGSVGElement>>
}) => {
  const missing = value === null || value === undefined || value === DASH
  return (
    <div className={cn('min-w-0 rounded-card border border-line bg-surface-1 px-3.5 py-3', className)}>
      <div className="flex items-center gap-1.5 text-[11px] font-medium tracking-wide text-fg-subtle uppercase">
        {Icon ? <Icon className="size-3.5" aria-hidden /> : null}
        <span className="truncate">{label}</span>
      </div>
      <div
        className={cn(
          'mt-1 truncate text-lg font-semibold tabular text-fg',
          missing && 'text-base font-normal text-fg-faint',
          !missing && tone && toneClasses[tone].text,
        )}>
        {missing ? 'not available' : value}
      </div>
      {hint ? <div className="mt-0.5 truncate text-xs text-fg-subtle">{hint}</div> : null}
    </div>
  )
}

export const StatGrid = ({ children, className }: { children: React.ReactNode; className?: string }) => (
  <div className={cn('grid grid-cols-2 gap-2.5 sm:grid-cols-3 xl:grid-cols-4', className)}>{children}</div>
)

// Horizontal usage bar. `ratio` null renders an empty, neutral track labelled unknown.
export const Meter = ({
  ratio,
  tone = 'accent',
  className,
  label,
}: {
  ratio: number | null | undefined
  tone?: Tone
  className?: string
  label?: string
}) => {
  const known = typeof ratio === 'number' && Number.isFinite(ratio)
  const pct = known ? Math.max(0, Math.min(1, ratio as number)) * 100 : 0
  return (
    <div
      role="meter"
      aria-label={label}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={known ? Math.round(pct) : undefined}
      aria-valuetext={known ? `${pct.toFixed(0)}%` : 'unknown'}
      className={cn('h-1.5 w-full overflow-hidden rounded-full bg-surface-3', !known && 'bg-[repeating-linear-gradient(45deg,transparent_0_4px,rgb(255_255_255/0.05)_4px_8px)]', className)}>
      {known ? <div className={cn('h-full rounded-full transition-[width] duration-500', toneClasses[tone].dot)} style={{ width: `${pct}%` }} /> : null}
    </div>
  )
}

// Tiny inline-SVG trend line. No chart library.
export const Sparkline = ({
  values,
  className,
  tone = 'accent',
  height = 32,
}: {
  values: (number | null)[]
  className?: string
  tone?: Tone
  height?: number
}) => {
  const points = values.map((v, i) => [i, v] as const).filter((p): p is readonly [number, number] => typeof p[1] === 'number' && Number.isFinite(p[1]))
  if (points.length < 2) return <div className={cn('h-8 rounded bg-surface-1', className)} style={{ height }} aria-hidden />
  const width = 100
  const min = Math.min(...points.map(p => p[1]))
  const max = Math.max(...points.map(p => p[1]))
  const span = max - min || 1
  const n = values.length - 1 || 1
  const xy = points.map(([i, v]) => [(i / n) * width, height - 2 - ((v - min) / span) * (height - 4)])
  const line = xy.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(2)},${y.toFixed(2)}`).join(' ')
  const area = `${line} L${xy[xy.length - 1][0].toFixed(2)},${height} L${xy[0][0].toFixed(2)},${height} Z`
  return (
    <svg viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" className={cn('w-full', toneClasses[tone].text, className)} style={{ height }} aria-hidden>
      <path d={area} fill="currentColor" opacity="0.12" />
      <path d={line} fill="none" stroke="currentColor" strokeWidth="1.5" vectorEffect="non-scaling-stroke" strokeLinejoin="round" />
    </svg>
  )
}

export interface BarDatum {
  label: string
  value: number | null
  tone?: Tone
}

// Proportional segments in one bar, with a legend. Unknown values are listed but not drawn.
export const SegmentBar = ({ data, className, total }: { data: BarDatum[]; className?: string; total?: number | null }) => {
  const known = data.filter(d => typeof d.value === 'number' && (d.value as number) > 0)
  const sum = total ?? known.reduce((acc, d) => acc + (d.value as number), 0)
  return (
    <div className={className}>
      <div className="flex h-2 w-full overflow-hidden rounded-full bg-surface-3">
        {sum > 0
          ? known.map(d => (
              <div
                key={d.label}
                title={d.label}
                className={cn('h-full first:rounded-l-full last:rounded-r-full', toneClasses[d.tone ?? 'accent'].dot)}
                style={{ width: `${((d.value as number) / sum) * 100}%` }}
              />
            ))
          : null}
      </div>
      <ul className="mt-2 flex flex-wrap gap-x-4 gap-y-1 text-xs text-fg-subtle">
        {data.map(d => (
          <li key={d.label} className="inline-flex items-center gap-1.5">
            <span className={cn('size-2 rounded-full', toneClasses[d.tone ?? 'accent'].dot)} />
            {d.label}
          </li>
        ))}
      </ul>
    </div>
  )
}
