export const dashboardCardSizes = ['1x1', '1x2', '2x1', '2x2', '3x1', '3x2', '4x2'] as const
export const dashboardCardVariants = ['compact', 'summary', 'hero', 'visual', 'graph'] as const

export type DashboardCardSize = (typeof dashboardCardSizes)[number]
export type DashboardCardVariant = (typeof dashboardCardVariants)[number]

export interface DashboardLayoutCard {
  instanceId: string
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
  variantSupportedSizes?: Partial<Record<DashboardCardVariant, DashboardCardSize[]>>
}

export const DASHBOARD_LAYOUT_STORAGE_KEY = 'vaulthalla.dashboard.layout.v1'

export interface DashboardPreferenceLayoutCard extends DashboardLayoutCard {
  instance_id: string
}

export interface DashboardPreferenceLayout {
  cards: DashboardPreferenceLayoutCard[]
}

export function dashboardLayoutInstanceId(id: string, variant: DashboardCardVariant, suffix?: number): string {
  return suffix && suffix > 0 ? `${id}:${variant}:${suffix}` : `${id}:${variant}`
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
  const normalized: DashboardLayoutCard[] = []
  const seenInstanceIds = new Set<string>()
  const seenVisibleCombos = new Set<string>()

  rawCards.forEach((rawCard, index) => {
    const card = asDashboardLayoutObject(rawCard)
    const id = typeof card.id === 'string' ? card.id : ''
    const catalogItem = catalogById.get(id)
    if (!catalogItem) return

    const rawVariant = card.variant
    const variant =
      isDashboardCardVariant(rawVariant) && catalogItem.supportedVariants.includes(rawVariant) ?
        rawVariant
      : catalogItem.defaultVariant
    const supportedSizes = catalogItem.variantSupportedSizes?.[variant] ?? catalogItem.supportedSizes
    const rawSize = card.size
    const size =
      isDashboardCardSize(rawSize) && supportedSizes.includes(rawSize) ?
        rawSize
      : supportedSizes.includes(catalogItem.defaultSize) ?
        catalogItem.defaultSize
      : supportedSizes[0] ?? catalogItem.defaultSize
    const comboKey = `${id}:${variant}`
    const visible = asBoolean(card.visible, true)

    if (visible && seenVisibleCombos.has(comboKey)) return

    const rawInstanceId = typeof card.instance_id === 'string' ? card.instance_id
      : typeof card.instanceId === 'string' ? card.instanceId
      : ''
    let instanceId = rawInstanceId || dashboardLayoutInstanceId(id, variant)
    let suffix = 1
    while (seenInstanceIds.has(instanceId)) {
      instanceId = dashboardLayoutInstanceId(id, variant, suffix++)
    }

    seenInstanceIds.add(instanceId)
    if (visible) seenVisibleCombos.add(comboKey)

    normalized.push({
      instanceId,
      id,
      size,
      variant,
      visible,
      order: asNumber(card.order, index),
    })
  })

  const defaultsByInstance = new Map(defaults.map(item => [item.instanceId, item]))
  for (const defaultCard of defaults) {
    if (seenInstanceIds.has(defaultCard.instanceId)) continue
    normalized.push({
      ...defaultCard,
      order: normalized.length + defaultCard.order,
    })
  }

  const repaired = normalized.some(card => card.visible) ?
    normalized
  : normalized.map(card => {
      const defaultCard = defaultsByInstance.get(card.instanceId)
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
      instanceId: card.instanceId,
      instance_id: card.instanceId,
      id: card.id,
      size: card.size,
      variant: card.variant,
      visible: card.visible,
      order,
    })),
  }
}
