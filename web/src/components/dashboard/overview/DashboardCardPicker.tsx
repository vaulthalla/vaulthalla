'use client'

import type {
  DashboardCardSize,
  DashboardCardVariant,
} from '@/models/dashboard/dashboardLayout'
import {
  dashboardCardSizes,
  dashboardCardVariants,
} from '@/models/dashboard/dashboardLayout'
import type { DashboardCardCatalogItem } from '@/components/dashboard/dashboardCardCatalog'
import { DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardSeverityTone } from '@/components/dashboard/dashboardSeverity'

export function DashboardCardPicker({
  catalog,
  visibleCombos,
  selectedSize,
  selectedVariant,
  onSizeChange,
  onVariantChange,
  onAdd,
}: {
  catalog: DashboardCardCatalogItem[]
  visibleCombos: Set<string>
  selectedSize: DashboardCardSize
  selectedVariant: DashboardCardVariant
  onSizeChange: (size: DashboardCardSize) => void
  onVariantChange: (variant: DashboardCardVariant) => void
  onAdd: (id: string, size: DashboardCardSize, variant: DashboardCardVariant) => void
}) {
  const candidates = catalog.filter(card => card.available)
  const sizeFiltered = candidates.filter(card => {
    const variantSizes = card.variantSupportedSizes?.[selectedVariant] ?? card.supportedSizes
    return variantSizes.includes(selectedSize)
  })
  const cardsToShow = sizeFiltered.length ? sizeFiltered : candidates

  return (
    <div className="absolute right-0 top-[calc(100%+0.6rem)] z-40 w-[min(42rem,calc(100vw-2rem))] rounded-3xl border border-white/10 bg-zinc-950/95 p-3 shadow-[0_30px_90px_-30px_rgba(0,0,0,0.95)] backdrop-blur-xl">
      <div className="flex flex-wrap items-start justify-between gap-3 border-b border-white/10 pb-3">
        <div>
          <div className="text-sm font-semibold text-white">Add dashboard card</div>
          <div className="mt-0.5 text-xs text-white/50">Pick a card with the size and presentation you want.</div>
        </div>
        <div className="flex flex-wrap gap-1.5">
          {dashboardCardSizes.map(size => (
            <button
              key={size}
              type="button"
              className={[
                'rounded-full border px-2 py-0.5 text-[11px] transition',
                selectedSize === size ?
                  'border-cyan-200/40 bg-cyan-400/15 text-cyan-50'
                : 'border-white/10 bg-white/5 text-white/55 hover:border-cyan-200/25 hover:text-cyan-100',
              ].join(' ')}
              onClick={() => onSizeChange(size)}>
              {size}
            </button>
          ))}
          <span className="mx-1 h-5 w-px bg-white/10" />
          {dashboardCardVariants.map(variant => (
            <button
              key={variant}
              type="button"
              className={[
                'rounded-full border px-2 py-0.5 text-[11px] capitalize transition',
                selectedVariant === variant ?
                  'border-cyan-200/40 bg-cyan-400/15 text-cyan-50'
                : 'border-white/10 bg-white/5 text-white/55 hover:border-cyan-200/25 hover:text-cyan-100',
              ].join(' ')}
              onClick={() => onVariantChange(variant)}>
              {variant}
            </button>
          ))}
        </div>
      </div>

      <div className="mt-3 grid max-h-[28rem] grid-cols-1 gap-2 overflow-y-auto pr-1 md:grid-cols-2">
        {cardsToShow.length ?
          cardsToShow.map(card => {
            const size = card.supportedSizes.includes(selectedSize) ? selectedSize : card.defaultSize
            const variantAvailable = card.supportedVariants.includes(selectedVariant)
            const variantSizes = card.variantSupportedSizes?.[variantAvailable ? selectedVariant : card.defaultVariant] ?? card.supportedSizes
            const normalizedSize = variantSizes.includes(size) ? size : variantSizes[0] ?? card.defaultSize
            const variant = variantAvailable ? selectedVariant : card.defaultVariant
            const tone = dashboardSeverityTone('unknown')
            const alreadyAdded = visibleCombos.has(`${card.id}:${variant}`)

            return (
              <button
                key={card.id}
                type="button"
                className={[
                  'group rounded-2xl border p-2.5 text-left transition',
                  alreadyAdded || !variantAvailable ?
                    'cursor-not-allowed border-white/10 bg-white/[0.025] opacity-55'
                  : 'border-white/10 bg-white/[0.035] hover:border-cyan-200/35 hover:bg-cyan-400/10',
                ].join(' ')}
                disabled={alreadyAdded || !variantAvailable}
                onClick={() => onAdd(card.id, normalizedSize, variant)}>
                <div className="flex items-start justify-between gap-2">
                  <div className="min-w-0">
                    <div className="flex items-center gap-1.5 text-sm font-semibold text-white/90">
                      <DashboardSeverityIcon severity="unknown" className={['h-3.5 w-3.5', tone.text].join(' ')} />
                      <span className="truncate">{card.title}</span>
                    </div>
                    <p className="mt-1 line-clamp-2 text-xs leading-snug text-white/50">{card.description}</p>
                  </div>
                  <span className="shrink-0 rounded-full border border-white/10 bg-black/20 px-2 py-0.5 text-[10px] text-white/55">
                    {!variantAvailable ? `No ${selectedVariant}`
                    : alreadyAdded ? 'Already added'
                    : `${normalizedSize} · ${variant}`}
                  </span>
                </div>
                <div className="mt-2 grid grid-cols-3 gap-1.5">
                  {['status', 'metric', 'detail'].map(item => (
                    <div key={item} className="rounded-lg border border-white/10 bg-black/20 px-2 py-1">
                      <div className="h-1 w-8 rounded-full bg-white/15" />
                      <div className="mt-1 h-2 w-12 rounded-full bg-white/10 group-hover:bg-cyan-200/20" />
                    </div>
                  ))}
                </div>
              </button>
            )
          })
        : <div className="rounded-2xl border border-white/10 bg-white/[0.03] p-4 text-sm text-white/55">
            Every available dashboard card is already visible.
          </div>}
      </div>
    </div>
  )
}
