'use client'

import React from 'react'
import Link from 'next/link'
import { cn } from '@/util/cn'
import { IconButton } from '@/components/ui/IconButton'
import { Select } from '@/components/ui/Field'
import { Skeleton } from '@/components/ui/State'
import { ArrowsUpDownLeftRightIcon, ChevronLeftIcon, ChevronRightIcon, XmarkIcon } from '@/components/ui/icons'
import { HealthBadge } from '@/features/health/components'
import { StackBar, TimeSeriesChart, SeriesLegend } from '@/features/health/charts'
import { sizesFor, type CardSize, type CardVariant, type CatalogCard, type LayoutCard } from '@/features/health/catalog'
import { healthHref, metricDisplay, metricNumber, metricTone, type Card, type Metric } from '@/features/health/model'

const dims = (size: CardSize) => {
  const [w, h] = size.split('x').map(Number)
  return { w: Math.min(4, Math.max(1, w || 2)), h: h === 2 ? 2 : 1 }
}

// Grid placement: quarter-width columns on xl, half on md, full width on phones.
const SPAN: Record<number, string> = {
  1: 'md:col-span-1 xl:col-span-1',
  2: 'md:col-span-2 xl:col-span-2',
  3: 'md:col-span-2 xl:col-span-3',
  4: 'md:col-span-2 xl:col-span-4',
}
const METRIC_COLS: Record<number, string> = {
  1: 'grid-cols-2',
  2: 'grid-cols-2 sm:grid-cols-4',
  3: 'grid-cols-2 sm:grid-cols-4 xl:grid-cols-6',
  4: 'grid-cols-2 sm:grid-cols-4 xl:grid-cols-6',
}
const METRIC_COUNT: Record<number, number> = { 1: 2, 2: 4, 3: 6, 4: 6 }

export const sizeLabel = (size: CardSize) => {
  const { w, h } = dims(size)
  return `${w === 4 ? 'Full' : `${w}/4`} width · ${h === 2 ? 'tall' : 'short'}`
}

// Which backend metrics to show: catalog priority first, quiet zeros and unmeasured values last.
const pickMetrics = (card: Card, item: CatalogCard, count: number, omit: Set<string>): Metric[] => {
  const byKey = new Map(card.metrics.map(m => [m.key, m]))
  const ordered = [...item.priority.map(k => byKey.get(k)).filter((m): m is Metric => Boolean(m)), ...card.metrics]
  const seen = new Set<string>()
  const unique = ordered.filter(m => !omit.has(m.key) && !seen.has(m.key) && seen.add(m.key))
  const quiet = (m: Metric) => (item.quietWhenZero?.includes(m.key) && metricNumber(m) === 0) || metricDisplay(m) === null
  return [...unique.filter(m => !quiet(m)), ...unique.filter(quiet)].slice(0, count)
}

const MetricCell = ({ metric, className }: { metric: Metric; className?: string }) => {
  const display = metricDisplay(metric)
  const tone = metricTone(metric)
  return (
    <div className={cn('min-w-0 rounded-control bg-surface-1 px-2.5 py-1.5', className)}>
      <div className="truncate text-[11px] text-fg-subtle">{metric.label}</div>
      <div
        className={cn(
          'truncate text-sm font-semibold text-fg tabular',
          display === null && 'font-normal text-fg-faint',
          tone === 'warn' && 'text-warn',
          tone === 'danger' && 'text-danger',
        )}>
        {display ?? 'not available'}
      </div>
    </div>
  )
}

const CardVisual = ({ card, item, tall }: { card: Card; item: CatalogCard; tall: boolean }) => {
  if (item.visual?.kind === 'stack') {
    const byKey = new Map(card.metrics.map(m => [m.key, m]))
    return (
      <StackBar
        className="py-1"
        data={item.visual.keys.map(([key, label]) => {
          const m = byKey.get(key)
          return { key, label, value: m ? metricNumber(m) : null }
        })}
      />
    )
  }
  const first = card.series.find(s => s.points.length > 0)
  const series = first ? card.series.filter(s => s.unit === first.unit && s.points.length > 0).slice(0, 3) : []
  return (
    <div>
      <TimeSeriesChart compact={!tall} series={series} unit={first?.unit ?? ''} height={tall ? 150 : 52} label={`${card.title} trend`} />
      {series.length > 1 && tall ? <SeriesLegend series={series} className="mt-1.5" /> : null}
    </div>
  )
}

export interface CustomizeHandlers {
  index: number
  total: number
  onMove: (instanceId: string, to: number) => void
  onRemove: (instanceId: string) => void
  onSize: (instanceId: string, size: CardSize) => void
  onVariant: (instanceId: string, variant: CardVariant) => void
  onDragStart: (instanceId: string) => void
  onDragEnter: (instanceId: string) => void
  onDragEnd: () => void
  dragging: boolean
  dropTarget: boolean
}

const CustomizeBar = ({ layout, item, h }: { layout: LayoutCard; item: CatalogCard; h: CustomizeHandlers }) => {
  const id = layout.instanceId
  return (
    <div className="relative z-10 -mx-1 -mt-1 mb-3 flex flex-wrap items-center gap-1.5 rounded-control border border-accent-line bg-accent-soft p-1.5">
      <button
        type="button"
        draggable
        onDragStart={event => {
          event.dataTransfer.effectAllowed = 'move'
          event.dataTransfer.setData('text/plain', id)
          h.onDragStart(id)
        }}
        onDragEnd={h.onDragEnd}
        onKeyDown={event => {
          const delta = event.key === 'ArrowUp' || event.key === 'ArrowLeft' ? -1 : event.key === 'ArrowDown' || event.key === 'ArrowRight' ? 1 : 0
          if (!delta) return
          event.preventDefault()
          h.onMove(id, h.index + delta)
        }}
        aria-label={`Move ${item.title} (position ${h.index + 1} of ${h.total}). Drag, or use the arrow keys.`}
        title="Drag, or focus and use the arrow keys"
        className="grid size-7 cursor-grab place-items-center rounded-[6px] text-accent-text hover:bg-surface-3 active:cursor-grabbing">
        <ArrowsUpDownLeftRightIcon className="size-3.5" aria-hidden />
      </button>
      <IconButton size="icon-sm" label="Move earlier" icon={ChevronLeftIcon} disabled={h.index === 0} onClick={() => h.onMove(id, h.index - 1)} className="size-7" />
      <IconButton
        size="icon-sm"
        label="Move later"
        icon={ChevronRightIcon}
        disabled={h.index >= h.total - 1}
        onClick={() => h.onMove(id, h.index + 1)}
        className="size-7"
      />
      <Select
        aria-label={`${item.title} size`}
        value={layout.size}
        onChange={event => h.onSize(id, event.target.value as CardSize)}
        className="h-7 w-auto py-0 pr-7 pl-2 text-xs">
        {sizesFor(item, layout.variant).map(size => (
          <option key={size} value={size}>
            {sizeLabel(size)}
          </option>
        ))}
      </Select>
      {item.supportedVariants.length > 1 ? (
        <Select
          aria-label={`${item.title} style`}
          value={layout.variant}
          onChange={event => h.onVariant(id, event.target.value as CardVariant)}
          className="h-7 w-auto py-0 pr-7 pl-2 text-xs">
          <option value="visual">Chart</option>
          <option value="tiles">Numbers</option>
        </Select>
      ) : null}
      <IconButton
        size="icon-sm"
        label={`Remove ${item.title}`}
        icon={XmarkIcon}
        disabled={h.total <= 1}
        onClick={() => h.onRemove(id)}
        className="ml-auto size-7 hover:text-danger"
      />
    </div>
  )
}

export const OverviewCard = ({
  layout,
  item,
  card,
  failed = false,
  customize,
}: {
  layout: LayoutCard
  item: CatalogCard
  card: Card | null
  // The overview request failed and there is no earlier answer: show unknown, not a loading state.
  failed?: boolean
  customize?: CustomizeHandlers
}) => {
  const { w, h } = dims(layout.size)
  const visual = layout.variant === 'visual' && item.visual ? item.visual : null
  const rows = h === 2 ? (visual ? 2 : 3) : visual ? 1 : 2
  const count = METRIC_COUNT[w] * rows
  const omit = new Set(visual?.kind === 'stack' ? visual.keys.map(([k]) => k) : [])
  const metrics = card ? pickMetrics(card, item, count, omit) : []
  const issues = card ? card.errors.length + card.warnings.length : 0
  const href = healthHref(card?.href || item.href)

  return (
    <article
      onDragOver={customize ? event => event.preventDefault() : undefined}
      onDragEnter={customize ? () => customize.onDragEnter(layout.instanceId) : undefined}
      onDrop={customize ? event => event.preventDefault() : undefined}
      className={cn(
        'panel group relative flex min-w-0 flex-col p-4 transition-[border-color,box-shadow,opacity] duration-150',
        SPAN[w],
        h === 2 && 'md:row-span-2',
        !customize && 'hover:border-line-strong',
        issues > 0 && card?.errors.length && 'border-danger-line',
        issues > 0 && !card?.errors.length && 'border-warn-line',
        customize?.dragging && 'opacity-50',
        customize?.dropTarget && 'shadow-glow',
      )}>
      {customize ? <CustomizeBar layout={layout} item={item} h={customize} /> : null}
      <header className="flex items-start justify-between gap-3">
        <h3 className="min-w-0 truncate text-sm font-semibold text-fg">
          {customize ? (
            card?.title || item.title
          ) : (
            <Link href={href} className="after:absolute after:inset-0 after:rounded-card focus-visible:outline-none focus-visible:after:outline-2 focus-visible:after:outline-accent">
              {card?.title || item.title}
            </Link>
          )}
        </h3>
        {card ? (
          <HealthBadge severity={card.severity} errors={card.errors.length} warnings={card.warnings.length} className="shrink-0" />
        ) : failed ? (
          <HealthBadge severity="unknown" className="shrink-0" />
        ) : (
          <Skeleton className="h-6 w-16 rounded-full" />
        )}
      </header>
      {card ? (
        <p className={cn('mt-1 text-xs text-fg-subtle', h === 2 ? 'line-clamp-2' : 'line-clamp-1')}>
          {card.available ? card.summary || item.description : (card.unavailableReason ?? 'This card is not available on this daemon.')}
        </p>
      ) : failed ? (
        <p className="mt-1 text-xs text-fg-subtle">Not available: the daemon didn&apos;t report this card.</p>
      ) : (
        <Skeleton className="mt-2 h-3 w-2/3" />
      )}
      <div className="mt-3 flex min-h-0 flex-1 flex-col justify-end gap-3">
        {visual && card?.available ? <CardVisual card={card} item={item} tall={h === 2} /> : null}
        {card ? (
          metrics.length ? (
            <div className={cn('grid gap-1.5', METRIC_COLS[w])}>
              {metrics.map((metric, i) => (
                <MetricCell key={metric.key} metric={metric} className={i >= 4 ? 'max-sm:hidden' : undefined} />
              ))}
            </div>
          ) : null
        ) : failed ? null : (
          <div className={cn('grid gap-1.5', METRIC_COLS[w])}>
            {Array.from({ length: Math.min(count, 4) }, (_, i) => (
              <Skeleton key={i} className="h-11 rounded-control" />
            ))}
          </div>
        )}
      </div>
    </article>
  )
}
