'use client'

import type React from 'react'

import type {
  DashboardCardSize,
  DashboardCardVariant,
  DashboardLayoutCard,
} from '@/models/dashboard/dashboardLayout'
import type { DashboardCardSummary } from '@/models/stats/dashboardOverview'
import type { DashboardCardCatalogItem } from '@/components/dashboard/dashboardCardCatalog'
import { DashboardHomeCard } from '@/components/dashboard/overview/DashboardHomeCard'

export interface DashboardVisibleCard {
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  card: DashboardCardSummary
}

export function DashboardGrid({
  visibleCards,
  customizing,
  draggedCardId,
  dragOverCardId,
  onMove,
  onDragStart,
  onDragOverCard,
  onDropCard,
  onDragEnd,
  onOpen,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  visibleCards: DashboardVisibleCard[]
  customizing: boolean
  draggedCardId: string | null
  dragOverCardId: string | null
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onDragOverCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDropCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDragEnd: () => void
  onOpen: (href: string) => void
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  return (
    <div className="grid grid-cols-1 gap-3 md:auto-rows-[5rem] md:grid-flow-dense md:grid-cols-6 lg:grid-cols-12">
      {visibleCards.map(({ card, layoutCard, catalogItem }, index) => (
        <DashboardHomeCard
          key={layoutCard.instanceId}
          card={card}
          layoutCard={layoutCard}
          catalogItem={catalogItem}
          customizing={customizing}
          index={index}
          total={visibleCards.length}
          onMove={onMove}
          onDragStart={onDragStart}
          onDragOverCard={onDragOverCard}
          onDropCard={onDropCard}
          onDragEnd={onDragEnd}
          onOpen={onOpen}
          dragging={draggedCardId === layoutCard.instanceId}
          dragOver={dragOverCardId === layoutCard.instanceId}
          onRemove={onRemove}
          onSizeChange={onSizeChange}
          onVariantChange={onVariantChange}
        />
      ))}
    </div>
  )
}
