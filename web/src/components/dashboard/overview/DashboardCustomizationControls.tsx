'use client'

import type React from 'react'

import type {
  DashboardCardSize,
  DashboardCardVariant,
  DashboardLayoutCard,
} from '@/models/dashboard/dashboardLayout'
import type { DashboardCardCatalogItem } from '@/components/dashboard/dashboardCardCatalog'

export function DashboardCustomizationControls({
  layoutCard,
  catalogItem,
  index,
  total,
  onMove,
  onDragStart,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  index: number
  total: number
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  const buttonClass =
    'rounded-full border border-white/10 bg-white/5 px-2 py-0.5 text-[11px] text-white/65 transition hover:border-cyan-200/30 hover:text-cyan-100 disabled:cursor-not-allowed disabled:opacity-35'
  const selectClass =
    'rounded-full border border-white/10 bg-zinc-950/80 px-2 py-0.5 text-[11px] text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50'

  return (
    <div className="mb-3 flex flex-wrap items-center gap-1.5 rounded-2xl border border-white/10 bg-black/20 p-1.5">
      <button
        type="button"
        draggable
        className="cursor-grab rounded-full border border-cyan-200/20 bg-cyan-400/10 px-2 py-0.5 text-[11px] text-cyan-100 transition hover:border-cyan-100/45 active:cursor-grabbing"
        title="Drag to reorder"
        onDragStart={event => onDragStart(layoutCard.instanceId, event)}>
        Drag
      </button>
      <button type="button" className={buttonClass} onClick={() => onMove(layoutCard.instanceId, -1)} disabled={index === 0}>
        Up
      </button>
      <button
        type="button"
        className={buttonClass}
        onClick={() => onMove(layoutCard.instanceId, 1)}
        disabled={index >= total - 1}>
        Down
      </button>
      <label className="flex items-center gap-2 text-xs text-white/45">
        Size
        <select
          className={selectClass}
          value={layoutCard.size}
          onChange={event => onSizeChange(layoutCard.instanceId, event.target.value as DashboardCardSize)}>
          {catalogItem.supportedSizes.map(size => (
            <option key={size} value={size}>
              {size}
            </option>
          ))}
        </select>
      </label>
      <label className="flex items-center gap-2 text-xs text-white/45">
        Variant
        <select
          className={selectClass}
          value={layoutCard.variant}
          onChange={event => onVariantChange(layoutCard.instanceId, event.target.value as DashboardCardVariant)}>
          {catalogItem.supportedVariants.map(variant => (
            <option key={variant} value={variant}>
              {variant}
            </option>
          ))}
        </select>
      </label>
      <button type="button" className={buttonClass} onClick={() => onRemove(layoutCard.instanceId)} disabled={total <= 1}>
        Remove
      </button>
    </div>
  )
}
