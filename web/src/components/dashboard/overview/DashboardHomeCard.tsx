'use client'

import type React from 'react'

import type {
  DashboardCardSize,
  DashboardCardVariant,
  DashboardLayoutCard,
} from '@/models/dashboard/dashboardLayout'
import type { DashboardCardSummary } from '@/models/stats/dashboardOverview'
import type { DashboardCardCatalogItem } from '@/components/dashboard/dashboardCardCatalog'
import { DashboardSeverityBadge, DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardSeverityTone, sortDashboardIssues } from '@/components/dashboard/dashboardSeverity'
import {
  dashboardGraphMetricKeys,
  dashboardVisualMetricKeys,
  isLowValueDashboardMetric,
  selectDashboardCardMetrics,
} from '@/components/dashboard/dashboardMetricCuration'
import { DashboardCustomizationControls } from '@/components/dashboard/overview/DashboardCustomizationControls'
import { DashboardCardVisual, DashboardGraphVisual } from '@/components/dashboard/overview/DashboardCardVisual'
import { DashboardMetricTile } from '@/components/dashboard/overview/DashboardMetricTile'
import {
  baseHeightForCard,
  gridClassForSize,
  metricCountForCard,
  metricGridClassForCard,
  visualHeightClassForCard,
} from '@/components/dashboard/overview/lib/layoutCapacity'

function DashboardInlineIssuePill({
  severity,
  errorCount,
  warningCount,
  firstIssue,
  compact = false,
}: {
  severity: DashboardCardSummary['severity']
  errorCount: number
  warningCount: number
  firstIssue?: { message: string } | null
  compact?: boolean
}) {
  if (!errorCount && !warningCount && !firstIssue) return null

  const tone = dashboardSeverityTone(severity)
  const countText =
    errorCount > 0 ? `${errorCount} error${errorCount === 1 ? '' : 's'}`
    : warningCount > 0 ? `${warningCount} warning${warningCount === 1 ? '' : 's'}`
    : 'notice'

  return (
    <span
      className={[
        'inline-flex min-w-0 items-center gap-1.5 rounded-full border px-2 py-0.5 text-[10px] leading-none',
        tone.border,
        tone.bg,
        tone.text,
      ].join(' ')}
      title={firstIssue?.message || countText}>
      <DashboardSeverityIcon severity={severity} className="h-3 w-3 shrink-0" />
      <span className="shrink-0 font-semibold">{countText}</span>
      {!compact && firstIssue ?
        <>
          <span className="text-white/30">·</span>
          <span className="max-w-48 truncate text-white/70">{firstIssue.message}</span>
        </>
      : null}
    </span>
  )
}

export function DashboardHomeCard({
  card,
  layoutCard,
  catalogItem,
  customizing,
  index,
  total,
  onMove,
  onDragStart,
  onDragOverCard,
  onDropCard,
  onDragEnd,
  onOpen,
  dragging,
  dragOver,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  card: DashboardCardSummary
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  customizing: boolean
  index: number
  total: number
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onDragOverCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDropCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDragEnd: () => void
  onOpen: (href: string) => void
  dragging: boolean
  dragOver: boolean
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  const tone = dashboardSeverityTone(card.severity)
  const errors = card.errors.length
  const warnings = card.warnings.length
  const firstIssue = sortDashboardIssues([...card.errors, ...card.warnings])[0]
  const isCompact = layoutCard.variant === 'compact' || layoutCard.size === '1x1' || layoutCard.size === '2x1'
  const isHero = layoutCard.variant === 'hero' || layoutCard.size === '4x2'
  const isGraph = layoutCard.variant === 'graph'
  const isVisual = layoutCard.variant === 'visual'
  const metricCount = metricCountForCard(layoutCard)
  const largeCard = layoutCard.size === '1x2' || layoutCard.size === '2x2' || layoutCard.size === '3x2' || layoutCard.size === '4x2'
  const meaningfulMetricCount = card.metrics.filter(metric => !isLowValueDashboardMetric(card.id, metric)).length
  const hasVisualFallback = dashboardVisualMetricKeys(card.id).size > 0
  const shouldRenderVisual = isGraph || isVisual || isHero || (largeCard && hasVisualFallback && meaningfulMetricCount < metricCount)
  const visualKeys =
    isGraph ? dashboardGraphMetricKeys(card.id)
    : shouldRenderVisual ? dashboardVisualMetricKeys(card.id)
    : new Set<string>()
  const selectedMetrics = selectDashboardCardMetrics(card, layoutCard, metricCount, visualKeys)
  const selectedMetricKeys = new Set(selectedMetrics.map(metric => metric.key))
  const hiddenMetricCount = card.metrics.filter(metric =>
    !selectedMetricKeys.has(metric.key) &&
    !visualKeys.has(metric.key) &&
    !isLowValueDashboardMetric(card.id, metric),
  ).length
  const visual =
    isGraph ? <DashboardGraphVisual card={card} compact={isCompact} />
    : shouldRenderVisual ? <DashboardCardVisual card={card} />
    : null
  const metricGridClass = metricGridClassForCard(layoutCard)
  const visualHeightClass = visualHeightClassForCard(layoutCard)

  return (
    <div
      className={['col-span-1', gridClassForSize(layoutCard.size)].join(' ')}
      onDragOver={event => onDragOverCard(layoutCard.instanceId, event)}
      onDrop={event => onDropCard(layoutCard.instanceId, event)}
      onDragEnd={onDragEnd}>
      <article
        role={!customizing ? 'link' : undefined}
        tabIndex={!customizing ? 0 : undefined}
        onClick={() => {
          if (!customizing && !dragging) onOpen(card.href || catalogItem.href)
        }}
        onKeyDown={event => {
          if (customizing) return
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault()
            onOpen(card.href || catalogItem.href)
          }
        }}
        className={[
          'group flex h-full max-h-full min-h-0 flex-col overflow-hidden rounded-2xl border bg-zinc-950/48 backdrop-blur transition hover:-translate-y-0.5 hover:brightness-110',
          !customizing ? 'cursor-pointer focus:outline-none focus-visible:ring-2 focus-visible:ring-cyan-200/45' : '',
          isHero ? 'p-3.5'
          : isGraph ? 'p-3'
          : isVisual ? 'p-3'
          : 'p-2.5',
          baseHeightForCard(layoutCard),
          tone.border,
          tone.ring,
          dragging ? 'opacity-55' : '',
          dragOver ? 'outline outline-2 outline-cyan-200/60' : '',
          errors > 0 || warnings > 0 ? 'ring-1 ring-inset' : '',
          errors > 0 ? 'ring-rose-300/15'
          : warnings > 0 ? 'ring-amber-300/15'
          : 'ring-white/0',
        ].join(' ')}>
        {customizing ?
          <DashboardCustomizationControls
            layoutCard={layoutCard}
            catalogItem={catalogItem}
            index={index}
            total={total}
            onMove={onMove}
            onDragStart={onDragStart}
            onRemove={onRemove}
            onSizeChange={onSizeChange}
            onVariantChange={onVariantChange}
          />
        : null}

        <div className="shrink-0">
          <div className="flex items-start justify-between gap-2">
            <div className="min-w-0">
              <div
                draggable
                className={['flex cursor-grab items-center gap-1.5 font-semibold text-white/90 active:cursor-grabbing', isHero ? 'text-lg' : isCompact ? 'text-sm' : 'text-base'].join(' ')}
                title="Drag card header to reorder"
                onDragStart={event => onDragStart(layoutCard.instanceId, event)}>
                <DashboardSeverityIcon severity={card.severity} className={[isHero ? 'h-5 w-5' : 'h-4 w-4', tone.text].join(' ')} />
                <span className="truncate">{card.title}</span>
              </div>
              {isHero ?
                <div className="mt-1 line-clamp-1 text-xs text-white/50">{card.description || catalogItem.description}</div>
              : null}
            </div>
            <div className="flex shrink-0 items-center gap-1.5">
              <DashboardInlineIssuePill
                severity={errors > 0 ? 'error' : warnings > 0 ? 'warning' : card.severity}
                errorCount={errors}
                warningCount={warnings}
                firstIssue={firstIssue}
                compact={isCompact}
              />
              {hiddenMetricCount > 0 ?
                <span
                  className="inline-flex shrink-0 items-center rounded-full border border-white/10 bg-white/[0.04] px-2 py-0.5 text-[10px] leading-none text-white/45"
                  title={`${hiddenMetricCount} lower-priority metrics hidden`}>
                  +{hiddenMetricCount}
                </span>
              : null}
              <DashboardSeverityBadge severity={card.severity} errorCount={errors} warningCount={warnings} compact={isCompact} />
            </div>
          </div>
          <p className={['mt-1.5 text-xs leading-snug text-white/70', isCompact ? 'line-clamp-1' : 'line-clamp-2'].join(' ')}>
            {card.available ? card.summary : card.unavailable_reason}
          </p>
        </div>

        <div className="mt-2 flex min-h-0 flex-1 flex-col gap-2 overflow-hidden">
          {visual ?
            <div
              className={[
                'min-h-0 shrink-0',
                visualHeightClass,
              ].join(' ')}>
              {visual}
            </div>
          : null}

          {selectedMetrics.length ?
            <div className={['grid min-h-0 shrink-0 gap-1.5 overflow-hidden', metricGridClass].join(' ')}>
              {selectedMetrics.map(metric => (
                <DashboardMetricTile key={`${card.id}-${metric.key}`} metric={metric} dense={!isHero} />
              ))}
            </div>
          : null}
        </div>
      </article>
    </div>
  )
}
