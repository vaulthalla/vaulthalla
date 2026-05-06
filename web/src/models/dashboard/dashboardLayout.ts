export const dashboardCardSizes = ['1x1', '2x1', '2x2', '3x2', '4x2'] as const
export const dashboardCardVariants = ['compact', 'summary', 'hero', 'visual'] as const

export type DashboardCardSize = (typeof dashboardCardSizes)[number]
export type DashboardCardVariant = (typeof dashboardCardVariants)[number]

export interface DashboardLayoutCard {
  id: string
  size: DashboardCardSize
  variant: DashboardCardVariant
  visible: boolean
  order: number
}

export interface DashboardLayoutCatalogItem {
  id: string
  defaultSize: DashboardCardSize
  defaultVariant: DashboardCardVariant
  supportedSizes: DashboardCardSize[]
  supportedVariants: DashboardCardVariant[]
}

export const DASHBOARD_LAYOUT_STORAGE_KEY = 'vaulthalla.dashboard.layout.v1'

export interface DashboardPreferenceLayout {
  cards: DashboardLayoutCard[]
}

export function isDashboardCardSize(value: unknown): value is DashboardCardSize {
  return typeof value === 'string' && dashboardCardSizes.includes(value as DashboardCardSize)
}

export function isDashboardCardVariant(value: unknown): value is DashboardCardVariant {
  return typeof value === 'string' && dashboardCardVariants.includes(value as DashboardCardVariant)
}

export function asDashboardLayoutObject(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {}
}

function asNumber(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback
}

function asBoolean(value: unknown, fallback: boolean): boolean {
  return typeof value === 'boolean' ? value : fallback
}

export function normalizeDashboardLayout(
  raw: unknown,
  catalog: DashboardLayoutCatalogItem[],
  defaults: DashboardLayoutCard[],
): DashboardLayoutCard[] {
  const rawObject = asDashboardLayoutObject(raw)
  const rawCards = Array.isArray(raw) ? raw : Array.isArray(rawObject.cards) ? rawObject.cards : []
  const catalogById = new Map(catalog.map(item => [item.id, item]))
  const defaultsById = new Map(defaults.map(item => [item.id, item]))
  const rawById = new Map<string, Record<string, unknown>>()

  for (const rawCard of rawCards) {
    const card = asDashboardLayoutObject(rawCard)
    const id = typeof card.id === 'string' ? card.id : ''
    if (catalogById.has(id)) rawById.set(id, card)
  }

  const normalized = catalog.map((catalogItem, index) => {
    const rawCard = rawById.get(catalogItem.id)
    const defaultCard = defaultsById.get(catalogItem.id)
    const rawSize = rawCard?.size
    const rawVariant = rawCard?.variant
    const size =
      isDashboardCardSize(rawSize) && catalogItem.supportedSizes.includes(rawSize) ?
        rawSize
      : defaultCard?.size ?? catalogItem.defaultSize
    const variant =
      isDashboardCardVariant(rawVariant) && catalogItem.supportedVariants.includes(rawVariant) ?
        rawVariant
      : defaultCard?.variant ?? catalogItem.defaultVariant

    return {
      id: catalogItem.id,
      size,
      variant,
      visible: rawCard ? asBoolean(rawCard.visible, defaultCard?.visible ?? false) : defaultCard?.visible ?? false,
      order: rawCard ? asNumber(rawCard.order, defaultCard?.order ?? index) : defaultCard?.order ?? index,
    }
  })

  const repaired = normalized.some(card => card.visible) ?
    normalized
  : normalized.map(card => {
      const defaultCard = defaultsById.get(card.id)
      return defaultCard?.visible ? { ...card, visible: true, order: defaultCard.order } : card
    })

  const visible = repaired
    .filter(card => card.visible)
    .sort((a, b) => a.order - b.order || a.id.localeCompare(b.id))
    .map((card, index) => ({ ...card, order: index }))
  const hidden = repaired
    .filter(card => !card.visible)
    .sort((a, b) => a.order - b.order || a.id.localeCompare(b.id))
    .map((card, index) => ({ ...card, order: visible.length + index }))

  return [...visible, ...hidden]
}

export function visibleDashboardLayoutCards(layout: DashboardLayoutCard[]): DashboardLayoutCard[] {
  return layout
    .filter(card => card.visible)
    .sort((a, b) => a.order - b.order || a.id.localeCompare(b.id))
}

export function dashboardLayoutPreference(layout: DashboardLayoutCard[]): DashboardPreferenceLayout {
  const visible = visibleDashboardLayoutCards(layout)
  const hidden = layout
    .filter(card => !card.visible)
    .sort((a, b) => a.order - b.order || a.id.localeCompare(b.id))

  return {
    cards: [...visible, ...hidden].map((card, order) => ({
      id: card.id,
      size: card.size,
      variant: card.variant,
      visible: card.visible,
      order,
    })),
  }
}
