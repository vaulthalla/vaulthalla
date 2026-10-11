// The overview card catalog and per-account layout. The catalog only decides which cards exist, how big they may
// be and which backend metrics to show first; every value and severity comes from stats.dashboard.overview.

import {
  dashboardLayoutInstanceId,
  dashboardLayoutPreference,
  normalizeDashboardLayout,
  visibleDashboardLayoutCards,
  type DashboardCardSize,
  type DashboardCardVariant,
  type DashboardLayoutCard,
  type DashboardLayoutCatalogItem,
} from '@/models/dashboard/dashboardLayout'
import type { DashboardOverviewRequest } from '@/models/stats/dashboardOverview'

export type CardSize = DashboardCardSize
export type CardVariant = DashboardCardVariant
export type LayoutCard = DashboardLayoutCard

// How a "visual" card draws itself: its backend trend series, or a proportional bar over a few of its metrics.
export type Visual = { kind: 'series' } | { kind: 'stack'; keys: [string, string][] }

export interface CatalogCard extends DashboardLayoutCatalogItem {
  title: string
  description: string
  href: string
  section: string
  // Metrics shown first, in order. The rest of the card's metrics follow.
  priority: string[]
  // Metrics that say nothing when they are 0 (they're skipped while other metrics can fill the slots).
  quietWhenZero?: string[]
  visual?: Visual
}

export const HERO_CARD_ID = 'system.health'

const ALL: CardSize[] = ['1x1', '1x2', '2x1', '2x2', '3x1', '3x2', '4x2']
const WIDE: CardSize[] = ['2x1', '2x2', '3x1', '3x2', '4x2']
const TILES: CardSize[] = ['1x1', '2x1', '3x1']

const card = (c: Omit<CatalogCard, 'defaultSize' | 'defaultVariant' | 'supportedSizes' | 'supportedVariants'> &
  Partial<Pick<CatalogCard, 'defaultSize' | 'defaultVariant' | 'supportedSizes' | 'supportedVariants'>>): CatalogCard => ({
  defaultSize: '2x1',
  defaultVariant: c.visual ? 'visual' : 'tiles',
  supportedSizes: ALL,
  supportedVariants: c.visual ? ['tiles', 'visual'] : ['tiles'],
  variantSupportedSizes: c.visual ? { tiles: TILES.concat(['2x2', '3x2', '4x2']), visual: WIDE.concat(['1x2']) } : { tiles: TILES },
  ...c,
})

export const CATALOG: CatalogCard[] = [
  card({
    id: 'system.threadpools',
    section: 'Runtime',
    title: 'Thread pools',
    description: 'Worker pressure across FUSE, sync, thumbnails, HTTP and stats.',
    href: '/health/runtime#thread-pools',
    priority: ['pressure', 'queue', 'saturated', 'pressured', 'workers', 'busy', 'idle', 'pools', 'stopped', 'degraded'],
    quietWhenZero: ['saturated', 'pressured', 'busy', 'stopped', 'degraded'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.connections',
    section: 'Runtime',
    title: 'Connections',
    description: 'Websocket session mix and unauthenticated buildup.',
    href: '/health/runtime#connections',
    priority: ['sessions', 'unauthenticated', 'share_pending', 'human', 'share', 'oldest_session', 'oldest_unauth', 'idle_timeout'],
    quietWhenZero: ['share_pending'],
    visual: { kind: 'stack', keys: [['human', 'Human'], ['share', 'Share'], ['unauthenticated', 'Unauthenticated']] },
  }),
  card({
    id: 'system.fuse',
    section: 'Filesystem',
    title: 'FUSE filesystem',
    description: 'Operation volume, errors, latency and open handles.',
    href: '/health/filesystem#fuse',
    priority: ['alertable_error_rate', 'ops', 'avg_latency', 'open_handles', 'alertable_errors', 'error_rate', 'max_latency', 'read_bytes', 'write_bytes', 'open_peak'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.fs_cache',
    section: 'Filesystem',
    title: 'FS cache',
    description: 'Filesystem cache hit rate, usage and churn.',
    href: '/health/filesystem#fs-cache',
    priority: ['hit_rate', 'requests', 'used', 'evictions', 'occupancy', 'hits', 'misses', 'inserts', 'avg_op'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.http_cache',
    section: 'Filesystem',
    title: 'Preview cache',
    description: 'HTTP preview cache hit rate, usage and churn.',
    href: '/health/filesystem#http-cache',
    priority: ['hit_rate', 'requests', 'used', 'evictions', 'occupancy', 'hits', 'misses', 'inserts', 'avg_op'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.storage',
    section: 'Storage',
    title: 'Storage backends',
    description: 'Local and S3 vault backends and free-space posture.',
    href: '/health/storage#storage-backend',
    priority: ['vaults', 'problem', 'backend_errors', 'degraded', 'inactive', 'healthy', 'local', 's3', 'providers'],
    visual: { kind: 'stack', keys: [['local', 'Local'], ['s3', 'S3'], ['inactive', 'Inactive']] },
  }),
  card({
    id: 'system.db',
    section: 'Storage',
    title: 'Database',
    description: 'Connectivity, connection pressure, cache hit ratio and size.',
    href: '/health/storage#database',
    priority: ['cache_hit', 'connections', 'size', 'oldest_tx', 'idle_tx_connections', 'deadlocks', 'active_connections', 'max_connections', 'slow_queries'],
    quietWhenZero: ['idle_tx_connections', 'deadlocks'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.retention',
    section: 'Storage',
    title: 'Retention & cleanup',
    description: 'Trash, audit, sync, share and cache cleanup backlog.',
    href: '/health/storage#retention',
    priority: ['overdue', 'trash', 'trash_bytes', 'sync_backlog', 'audit_backlog', 'cache_expired', 'sync_events', 'cache_entries', 'oldest_trash'],
    visual: { kind: 'stack', keys: [['overdue', 'Overdue'], ['sync_backlog', 'Sync backlog'], ['audit_backlog', 'Audit backlog'], ['cache_expired', 'Expired cache']] },
  }),
  card({
    id: 'system.operations',
    section: 'Activity',
    title: 'Operation queue',
    description: 'Pending, active, failed and stalled filesystem and share work.',
    href: '/health/activity#operation-queue',
    defaultSize: '2x2',
    priority: ['pending', 'in_progress', 'stalled', 'failed_24h', 'active_uploads', 'stalled_uploads', 'failed_uploads_24h', 'oldest_pending', 'oldest_active', 'success_ops', 'error_ops'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.trends',
    section: 'Activity',
    title: 'Trend samples',
    description: 'Collection health of the stats snapshot series.',
    href: '/health/activity#trends',
    priority: ['latest_sample_age', 'coverage', 'window', 'points'],
    visual: { kind: 'series' },
  }),
  card({
    id: 'system.pricing_budget',
    section: 'Cost',
    title: 'S3 budgets',
    description: 'Active policies, blocked syncs, spend and projection.',
    href: '/cost',
    priority: ['monthly_spend', 'projected_monthly', 'active_policies', 'blocked_syncs', 'warnings', 'critical', 'pending_overrides'],
  }),
  card({
    id: 'system.pricing_providers',
    section: 'Cost',
    title: 'Provider spend',
    description: 'AWS S3 and Cloudflare R2 spend against provider budgets.',
    href: '/cost',
    priority: ['aws-s3_current', 'aws-s3_projected', 'cloudflare-r2_current', 'cloudflare-r2_projected', 'provider_policies'],
  }),
  card({
    id: 'system.pricing_vaults',
    section: 'Cost',
    title: 'Vault spend',
    description: 'Top vaults by committed spend and projected overage.',
    href: '/cost',
    priority: ['top_spend', 'top_vault', 'tracked_vaults', 'projected_overage_vaults'],
  }),
  card({
    id: 'system.pricing_alerts',
    section: 'Cost',
    title: 'Budget alerts',
    description: 'Active warning and critical budget notifications.',
    href: '/cost',
    priority: ['unacknowledged', 'critical', 'warnings', 'active_alerts'],
  }),
  card({
    id: 'system.pricing_catalog',
    section: 'Cost',
    title: 'Pricing catalog',
    description: 'Catalog verification, staleness and provider coverage.',
    href: '/cost',
    priority: ['catalog_issues', 'stale', 'unverified', 'unsupported'],
  }),
  card({
    id: 'system.pricing_overrides',
    section: 'Cost',
    title: 'Blocks & overrides',
    description: 'Blocked syncs and pending, approved and denied override requests.',
    href: '/cost',
    priority: ['blocked_syncs', 'pending', 'approved', 'denied', 'used'],
  }),
]

export const CATALOG_BY_ID = new Map(CATALOG.map(c => [c.id, c]))

export const sizesFor = (item: CatalogCard, variant: CardVariant): CardSize[] =>
  item.variantSupportedSizes?.[variant] ?? item.supportedSizes

type PresetCard = Pick<LayoutCard, 'id' | 'size' | 'variant'>

export interface Preset {
  id: string
  title: string
  cards: PresetCard[]
}

const DEFAULT_CARDS: PresetCard[] = [
  { id: 'system.operations', size: '4x2', variant: 'visual' },
  { id: 'system.storage', size: '2x1', variant: 'visual' },
  { id: 'system.pricing_budget', size: '2x1', variant: 'tiles' },
  { id: 'system.threadpools', size: '2x1', variant: 'visual' },
  { id: 'system.fuse', size: '2x1', variant: 'visual' },
  { id: 'system.db', size: '2x1', variant: 'visual' },
  { id: 'system.retention', size: '2x1', variant: 'visual' },
  { id: 'system.connections', size: '2x1', variant: 'visual' },
  { id: 'system.fs_cache', size: '2x1', variant: 'visual' },
]

export const PRESETS: Preset[] = [
  { id: 'default', title: 'Default', cards: DEFAULT_CARDS },
  {
    id: 'minimal',
    title: 'Minimal',
    cards: [
      { id: 'system.operations', size: '2x1', variant: 'tiles' },
      { id: 'system.storage', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
      { id: 'system.threadpools', size: '2x1', variant: 'visual' },
    ],
  },
  {
    id: 'runtime',
    title: 'Runtime',
    cards: [
      { id: 'system.threadpools', size: '4x2', variant: 'visual' },
      { id: 'system.connections', size: '2x1', variant: 'visual' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.operations', size: '2x1', variant: 'tiles' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
    ],
  },
  {
    id: 'storage',
    title: 'Storage',
    cards: [
      { id: 'system.storage', size: '2x2', variant: 'visual' },
      { id: 'system.db', size: '2x2', variant: 'visual' },
      { id: 'system.retention', size: '2x1', variant: 'visual' },
      { id: 'system.fs_cache', size: '2x1', variant: 'visual' },
      { id: 'system.http_cache', size: '2x1', variant: 'visual' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
    ],
  },
  {
    id: 'cost_control',
    title: 'Cost control',
    cards: [
      { id: 'system.pricing_budget', size: '2x1', variant: 'tiles' },
      { id: 'system.pricing_alerts', size: '2x1', variant: 'tiles' },
      { id: 'system.pricing_providers', size: '2x1', variant: 'tiles' },
      { id: 'system.pricing_vaults', size: '2x1', variant: 'tiles' },
      { id: 'system.pricing_catalog', size: '2x1', variant: 'tiles' },
      { id: 'system.pricing_overrides', size: '2x1', variant: 'tiles' },
    ],
  },
  {
    id: 'cockpit',
    title: 'Cockpit',
    cards: [
      { id: 'system.operations', size: '2x2', variant: 'visual' },
      { id: 'system.threadpools', size: '2x2', variant: 'visual' },
      { id: 'system.storage', size: '1x1', variant: 'tiles' },
      { id: 'system.connections', size: '1x1', variant: 'tiles' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
      { id: 'system.retention', size: '2x1', variant: 'visual' },
      { id: 'system.fs_cache', size: '2x1', variant: 'visual' },
      { id: 'system.http_cache', size: '2x1', variant: 'visual' },
      { id: 'system.trends', size: '2x1', variant: 'visual' },
    ],
  },
]

export const layoutFromCards = (cards: PresetCard[]): LayoutCard[] =>
  normalizeLayout(
    cards.map((c, order) => ({ ...c, instanceId: dashboardLayoutInstanceId(c.id, c.variant), visible: true, order })),
  )

export const defaultLayout = (): LayoutCard[] =>
  normalizeDashboardLayout(
    DEFAULT_CARDS.map((c, order) => ({ ...c, instanceId: dashboardLayoutInstanceId(c.id, c.variant), visible: true, order })),
    CATALOG,
    [],
  )

// Repairs anything a stored layout can get wrong (unknown cards, unsupported sizes, duplicates). No defaults are
// merged in, so a removed card stays removed; a layout with nothing visible falls back to the default board.
export const normalizeLayout = (raw: unknown): LayoutCard[] => {
  const layout = normalizeDashboardLayout(raw, CATALOG, [])
  return layout.some(card => card.visible) ? layout : defaultLayout()
}

export const visibleCards = visibleDashboardLayoutCards

export const layoutKey = (layout: LayoutCard[]) => JSON.stringify(dashboardLayoutPreference(layout).cards)

export const toPreference = dashboardLayoutPreference

export const overviewPayload = (layout: LayoutCard[]): DashboardOverviewRequest => ({
  scope: 'system',
  mode: 'dashboard_home',
  cards: [
    { id: HERO_CARD_ID, size: '3x2', variant: 'hero' },
    ...visibleDashboardLayoutCards(layout)
      .filter(c => c.id !== HERO_CARD_ID)
      .map(c => ({ id: c.id, size: c.size, variant: c.variant })),
  ],
})

// Only visible cards are kept: a hidden card in the stored layout is the same as one not added.
export const withMoved = (layout: LayoutCard[], instanceId: string, to: number): LayoutCard[] => {
  const visible = visibleDashboardLayoutCards(layout)
  const from = visible.findIndex(c => c.instanceId === instanceId)
  if (from < 0 || to < 0 || to >= visible.length || from === to) return layout
  const next = [...visible]
  const [moved] = next.splice(from, 1)
  next.splice(to, 0, moved)
  return next.map((c, order) => ({ ...c, order }))
}
