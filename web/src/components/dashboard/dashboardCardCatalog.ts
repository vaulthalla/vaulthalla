import { dashboardLayoutInstanceId, type DashboardLayoutCard, type DashboardLayoutCatalogItem } from '@/models/dashboard/dashboardLayout'
import {
  dashboardCardDefinitions,
  dashboardLayoutPresetDefinitions,
  defaultDashboardLayoutDefinitionCards,
  type DashboardLayoutPresetDefinition,
} from '@/components/dashboard/dashboardCardDefinitions'

export interface DashboardCardCatalogItem extends DashboardLayoutCatalogItem {
  sectionId: string
  title: string
  description: string
  href: string
  available: boolean
  unavailableReason: string | null
}

export type DashboardLayoutPreset = DashboardLayoutPresetDefinition

export const dashboardCardCatalog: DashboardCardCatalogItem[] = dashboardCardDefinitions.map(definition => ({
  id: definition.id,
  sectionId: definition.sectionId,
  title: definition.title,
  description: definition.description,
  href: definition.href,
  defaultVariant: definition.defaultVariant,
  defaultSize: definition.defaultSize,
  supportedSizes: definition.supportedSizes,
  supportedVariants: definition.supportedVariants,
  variantSupportedSizes: Object.fromEntries(
    definition.supportedVariants.map(variant => [
      variant,
      definition.variants[variant]?.supportedSizes ?? definition.supportedSizes,
    ]),
  ),
  available: true,
  unavailableReason: null,
}))

export const dashboardLayoutPresets: DashboardLayoutPreset[] = dashboardLayoutPresetDefinitions

export function dashboardLayoutFromCards(cards: DashboardLayoutPreset['cards']): DashboardLayoutCard[] {
  return cards.map((card, order) => ({
    instanceId: dashboardLayoutInstanceId(card.id, card.variant),
    id: card.id,
    size: card.size,
    variant: card.variant,
    visible: true,
    order,
  }))
}

export function defaultDashboardLayout(): DashboardLayoutCard[] {
  return dashboardLayoutFromCards(defaultDashboardLayoutDefinitionCards())
}

export function dashboardCardCatalogById(): Map<string, DashboardCardCatalogItem> {
  return new Map(dashboardCardCatalog.map(card => [card.id, card]))
}
