import type { DashboardCardSize, DashboardCardVariant, DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'
import type { DashboardCardSummary, DashboardMetricSummary } from '@/models/stats/dashboardOverview'
import type {
  DashboardCardDefinition,
  DashboardCardVariantDefinition,
  DashboardCardVisualKind,
} from '@/components/dashboard/dashboardCardDefinitions'
import {
  dashboardVisualMetricKeys,
  isLowValueDashboardMetric,
  selectDashboardCardMetrics,
} from '@/components/dashboard/dashboardMetricCuration'
import {
  templateForCardSize,
  type DashboardCardSizeTemplate,
} from '@/components/dashboard/overview/lib/layoutCapacity'

export interface DashboardCardRenderPlan {
  definition: DashboardCardDefinition
  layoutCard: DashboardLayoutCard
  normalizedSize: DashboardCardSize
  normalizedVariant: DashboardCardVariant
  supportsCurrentSize: boolean
  supportsCurrentVariant: boolean
  normalized: boolean
  template: DashboardCardSizeTemplate
  variantDefinition: DashboardCardVariantDefinition
  isCompact: boolean
  isHero: boolean
  visualKind: DashboardCardVisualKind | null
  selectedMetrics: DashboardMetricSummary[]
  hiddenMetricCount: number
  visualMetricKeys: Set<string>
  metricSlotCount: number
  cardGridClass: string
  cardHeightRem: number
  cardHeightClass: string
  metricGridClass: string
  visualHeightClass: string
  visualContainerClass: string
}

export function buildDashboardCardRenderPlan({
  definition,
  layoutCard,
  card,
}: {
  definition: DashboardCardDefinition
  layoutCard: DashboardLayoutCard
  card: DashboardCardSummary
}): DashboardCardRenderPlan {
  const supportsCurrentVariant = definition.supportedVariants.includes(layoutCard.variant)
  const normalizedVariant = supportsCurrentVariant ? layoutCard.variant : definition.defaultVariant
  const variantDefinition = definition.variants[normalizedVariant] ?? {}
  const supportedSizes = variantDefinition.supportedSizes ?? definition.supportedSizes
  const supportsCurrentSize = supportedSizes.includes(layoutCard.size)
  const normalizedSize =
    supportsCurrentSize ? layoutCard.size
    : supportedSizes.includes(definition.defaultSize) ? definition.defaultSize
    : supportedSizes[0] ?? definition.defaultSize
  const template = templateForCardSize(normalizedSize)
  const visualKind = variantDefinition.visualKind ?? definition.visualKind ?? null
  const isHero = false
  const isSingleRow = normalizedSize.endsWith('x1')
  const isCompact = isSingleRow || normalizedSize === '1x1'
  const shouldUseVisual = Boolean(visualKind && normalizedVariant === 'visual')
  const metricSlotCount = shouldUseVisual && isSingleRow ? 0 : template.tileSlotCount
  const visualMetricKeys = shouldUseVisual ? dashboardVisualMetricKeys(card.id, normalizedVariant) : new Set<string>()
  const normalizedLayoutCard = {
    ...layoutCard,
    size: normalizedSize,
    variant: normalizedVariant,
  }
  const selectedMetrics = selectDashboardCardMetrics(card, normalizedLayoutCard, metricSlotCount, visualMetricKeys, {
    backfillLowValue: true,
    backfillOmitted: true,
  })
  const selectedMetricKeys = new Set(selectedMetrics.map(metric => metric.key))
  const hiddenMetricCount = metricSlotCount > 0 ?
    card.metrics.filter(metric =>
      !selectedMetricKeys.has(metric.key) &&
      !visualMetricKeys.has(metric.key) &&
      !isLowValueDashboardMetric(card.id, metric),
    ).length
  : 0
  const visualContainerClass =
    shouldUseVisual ?
      isSingleRow ? 'min-h-0 flex-1'
      : 'min-h-0 flex-1'
    : ''

  return {
    definition,
    layoutCard,
    normalizedSize,
    normalizedVariant,
    supportsCurrentSize,
    supportsCurrentVariant,
    normalized: normalizedSize !== layoutCard.size || normalizedVariant !== layoutCard.variant,
    template,
    variantDefinition,
    isCompact,
    isHero,
    visualKind: shouldUseVisual ? visualKind : null,
    selectedMetrics,
    hiddenMetricCount,
    visualMetricKeys,
    metricSlotCount,
    cardGridClass: template.cardGridClass,
    cardHeightRem: template.cardHeightRem,
    cardHeightClass: template.cardHeightClass,
    metricGridClass: template.metricGridClass,
    visualHeightClass: template.visualHeightClass,
    visualContainerClass,
  }
}
