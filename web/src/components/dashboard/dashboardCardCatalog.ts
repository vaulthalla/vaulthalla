import type {
  DashboardCardSize,
  DashboardCardVariant,
  DashboardLayoutCard,
  DashboardLayoutCatalogItem,
} from '@/models/dashboard/dashboardLayout'

export interface DashboardCardCatalogItem extends DashboardLayoutCatalogItem {
  sectionId: string
  title: string
  description: string
  href: string
  available: boolean
  unavailableReason: string | null
}

export interface DashboardLayoutPreset {
  id: string
  title: string
  description: string
  cards: Array<Pick<DashboardLayoutCard, 'id' | 'size' | 'variant'>>
}

const allSizes: DashboardCardSize[] = ['1x1', '2x1', '2x2', '3x2', '4x2']
const allVariants: DashboardCardVariant[] = ['compact', 'summary', 'hero']
const compactSizes: DashboardCardSize[] = ['1x1', '2x1', '2x2']
const summarySizes: DashboardCardSize[] = ['2x1', '2x2', '3x2']
const wideSizes: DashboardCardSize[] = ['2x2', '3x2', '4x2']

export const dashboardCardCatalog: DashboardCardCatalogItem[] = [
  {
    id: 'system.health',
    sectionId: 'runtime',
    title: 'System Health',
    description: 'Core runtime, protocol, dependency, FUSE, and shell readiness.',
    href: '/dashboard/runtime#system-health',
    defaultVariant: 'hero',
    defaultSize: '4x2',
    supportedSizes: wideSizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.threadpools',
    sectionId: 'runtime',
    title: 'Thread Pools',
    description: 'Runtime worker pressure across FUSE, sync, thumbnails, HTTP, and stats.',
    href: '/dashboard/runtime#thread-pools',
    defaultVariant: 'summary',
    defaultSize: '2x1',
    supportedSizes: summarySizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.connections',
    sectionId: 'runtime',
    title: 'Connection Health',
    description: 'Websocket session mix and unauthenticated buildup.',
    href: '/dashboard/runtime#connections',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: compactSizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.fuse',
    sectionId: 'filesystem',
    title: 'FUSE Filesystem',
    description: 'Live filesystem operation volume, errors, latency, and open handles.',
    href: '/dashboard/filesystem#fuse',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: summarySizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.fs_cache',
    sectionId: 'filesystem',
    title: 'FS Cache',
    description: 'Filesystem cache hit rate, usage, and churn.',
    href: '/dashboard/filesystem#fs-cache',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: compactSizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.http_cache',
    sectionId: 'filesystem',
    title: 'HTTP Preview Cache',
    description: 'Preview cache hit rate, usage, and churn.',
    href: '/dashboard/filesystem#http-cache',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: compactSizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.storage',
    sectionId: 'storage',
    title: 'Storage Backend',
    description: 'Local and S3 vault backend configuration and free-space posture.',
    href: '/dashboard/storage#storage-backend',
    defaultVariant: 'summary',
    defaultSize: '2x2',
    supportedSizes: allSizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.db',
    sectionId: 'storage',
    title: 'Database Health',
    description: 'Database connectivity, connection pressure, cache hit ratio, and table size.',
    href: '/dashboard/storage#database',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: summarySizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.retention',
    sectionId: 'storage',
    title: 'Retention / Cleanup',
    description: 'Trash, audit, sync, share, and cache cleanup backlog.',
    href: '/dashboard/storage#retention',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: summarySizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.operations',
    sectionId: 'operations',
    title: 'Operation Queue',
    description: 'Pending, active, failed, and stalled filesystem/share work.',
    href: '/dashboard/operations#operation-queue',
    defaultVariant: 'summary',
    defaultSize: '2x2',
    supportedSizes: allSizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
  {
    id: 'system.trends',
    sectionId: 'trends',
    title: 'Trends',
    description: 'Recently collected stats snapshot series.',
    href: '/dashboard/trends#trends',
    defaultVariant: 'compact',
    defaultSize: '2x1',
    supportedSizes: summarySizes,
    supportedVariants: allVariants,
    available: true,
    unavailableReason: null,
  },
]

const defaultVisibleCards: DashboardLayoutPreset['cards'] = [
  { id: 'system.health', size: '4x2', variant: 'hero' },
  { id: 'system.operations', size: '2x2', variant: 'summary' },
  { id: 'system.storage', size: '2x2', variant: 'summary' },
  { id: 'system.fuse', size: '2x1', variant: 'compact' },
  { id: 'system.connections', size: '2x1', variant: 'compact' },
  { id: 'system.db', size: '2x1', variant: 'compact' },
  { id: 'system.retention', size: '2x1', variant: 'compact' },
  { id: 'system.trends', size: '2x1', variant: 'compact' },
]

export const dashboardLayoutPresets: DashboardLayoutPreset[] = [
  {
    id: 'default',
    title: 'Default',
    description: 'Balanced command-center view across runtime, operations, storage, filesystem, and trends.',
    cards: defaultVisibleCards,
  },
  {
    id: 'minimal',
    title: 'Minimal',
    description: 'Small health-first board for operators who mostly use drilldowns.',
    cards: [
      { id: 'system.health', size: '4x2', variant: 'hero' },
      { id: 'system.operations', size: '2x1', variant: 'compact' },
      { id: 'system.storage', size: '2x1', variant: 'compact' },
      { id: 'system.trends', size: '2x1', variant: 'compact' },
    ],
  },
  {
    id: 'runtime',
    title: 'Runtime',
    description: 'Runtime, workers, sessions, and filesystem pressure.',
    cards: [
      { id: 'system.health', size: '4x2', variant: 'hero' },
      { id: 'system.threadpools', size: '2x2', variant: 'summary' },
      { id: 'system.connections', size: '2x1', variant: 'compact' },
      { id: 'system.fuse', size: '2x1', variant: 'compact' },
      { id: 'system.operations', size: '2x1', variant: 'compact' },
    ],
  },
  {
    id: 'storage',
    title: 'Storage',
    description: 'Backing providers, database, retention, and cache posture.',
    cards: [
      { id: 'system.health', size: '4x2', variant: 'hero' },
      { id: 'system.storage', size: '2x2', variant: 'summary' },
      { id: 'system.db', size: '2x2', variant: 'summary' },
      { id: 'system.retention', size: '2x1', variant: 'compact' },
      { id: 'system.fs_cache', size: '2x1', variant: 'compact' },
      { id: 'system.http_cache', size: '2x1', variant: 'compact' },
    ],
  },
  {
    id: 'operations',
    title: 'Operations',
    description: 'Queued work, transfers, FUSE activity, and worker pressure.',
    cards: [
      { id: 'system.health', size: '4x2', variant: 'hero' },
      { id: 'system.operations', size: '3x2', variant: 'hero' },
      { id: 'system.threadpools', size: '2x2', variant: 'summary' },
      { id: 'system.fuse', size: '2x1', variant: 'compact' },
      { id: 'system.connections', size: '2x1', variant: 'compact' },
    ],
  },
  {
    id: 'cockpit',
    title: 'Cockpit',
    description: 'Wide high-signal overview for wallboard-style monitoring.',
    cards: [
      { id: 'system.health', size: '4x2', variant: 'hero' },
      { id: 'system.operations', size: '2x2', variant: 'summary' },
      { id: 'system.storage', size: '2x2', variant: 'summary' },
      { id: 'system.threadpools', size: '2x1', variant: 'compact' },
      { id: 'system.fuse', size: '2x1', variant: 'compact' },
      { id: 'system.db', size: '2x1', variant: 'compact' },
      { id: 'system.retention', size: '2x1', variant: 'compact' },
      { id: 'system.connections', size: '2x1', variant: 'compact' },
      { id: 'system.trends', size: '2x1', variant: 'compact' },
    ],
  },
]

export function dashboardLayoutFromCards(cards: DashboardLayoutPreset['cards']): DashboardLayoutCard[] {
  const visibleById = new Map(cards.map((card, order) => [card.id, { ...card, order }]))

  return dashboardCardCatalog.map((catalogItem, index) => {
    const visible = visibleById.get(catalogItem.id)
    return {
      id: catalogItem.id,
      size: visible?.size ?? catalogItem.defaultSize,
      variant: visible?.variant ?? catalogItem.defaultVariant,
      visible: Boolean(visible),
      order: visible?.order ?? cards.length + index,
    }
  })
}

export function defaultDashboardLayout(): DashboardLayoutCard[] {
  return dashboardLayoutFromCards(defaultVisibleCards)
}

export function dashboardCardCatalogById(): Map<string, DashboardCardCatalogItem> {
  return new Map(dashboardCardCatalog.map(card => [card.id, card]))
}
