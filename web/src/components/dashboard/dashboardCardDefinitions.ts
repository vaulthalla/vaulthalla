import type {
  DashboardCardSize,
  DashboardCardVariant,
  DashboardLayoutCard,
} from '@/models/dashboard/dashboardLayout'

export type DashboardCardVisualKind =
  | 'stack:operations'
  | 'stack:connections'
  | 'stack:storage'
  | 'stack:retention'
  | 'meter:fuse_error'
  | 'meter:cache_hit'
  | 'meter:db_cache'
  | 'meter:thread_pressure'
  | 'trend:coverage'
  | 'sparkline:thread_pressure'
  | 'sparkline:fuse_error'
  | 'sparkline:cache_hit'
  | 'sparkline:db_cache'
  | 'sparkline:coverage'

export interface DashboardCardVariantDefinition {
  metricPriority?: string[]
  visualKind?: DashboardCardVisualKind
  supportedSizes?: DashboardCardSize[]
  omitMetricsWhenVisual?: string[]
  requiresSeries?: boolean
}

export interface DashboardCardDefinition {
  id: string
  sectionId: string
  title: string
  description: string
  href: string
  defaultSize: DashboardCardSize
  defaultVariant: DashboardCardVariant
  supportedSizes: DashboardCardSize[]
  supportedVariants: DashboardCardVariant[]
  metricPriority: string[]
  lowValueMetricKeys?: string[]
  zeroLowValueMetricKeys?: string[]
  visualKind?: DashboardCardVisualKind
  omitMetricsWhenVisual?: string[]
  variants: Partial<Record<DashboardCardVariant, DashboardCardVariantDefinition>>
}

export interface DashboardLayoutPresetDefinition {
  id: string
  title: string
  description: string
  cards: Array<Pick<DashboardLayoutCard, 'id' | 'size' | 'variant'>>
}

const y2Sizes: DashboardCardSize[] = ['1x2', '2x2', '3x2', '4x2']
const y1AndY2Sizes: DashboardCardSize[] = ['1x1', '1x2', '2x1', '2x2', '3x1', '3x2', '4x2']
const summarySizes: DashboardCardSize[] = ['2x1', '3x1']
const visualSizes: DashboardCardSize[] = ['2x1', '2x2', '3x1', '3x2', '4x2']
const graphSizes: DashboardCardSize[] = ['2x1', '2x2', '3x1', '3x2', '4x2']

function commonVariants(
  visualKind: DashboardCardVisualKind,
  graphKind?: DashboardCardVisualKind,
): Partial<Record<DashboardCardVariant, DashboardCardVariantDefinition>> {
  return {
    compact: { supportedSizes: ['1x1', '2x1', '3x1'] },
    summary: { supportedSizes: summarySizes },
    hero: { supportedSizes: y2Sizes, visualKind },
    visual: { supportedSizes: visualSizes, visualKind },
    ...(graphKind ?
      {
        graph: {
          supportedSizes: graphSizes,
          visualKind: graphKind,
          requiresSeries: true,
        },
      }
    : {}),
  }
}

export const dashboardCardDefinitions: DashboardCardDefinition[] = [
  {
    id: 'system.threadpools',
    sectionId: 'runtime',
    title: 'Thread Pools',
    description: 'Runtime worker pressure across FUSE, sync, thumbnails, HTTP, and stats.',
    href: '/dashboard/runtime#thread-pools',
    defaultVariant: 'graph',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'hero', 'visual', 'graph'],
    metricPriority: ['queue', 'pressure', 'workers', 'idle', 'pressured', 'saturated', 'borrowed'],
    visualKind: 'meter:thread_pressure',
    omitMetricsWhenVisual: ['pressure'],
    variants: {
      ...commonVariants('meter:thread_pressure', 'sparkline:thread_pressure'),
      graph: {
        supportedSizes: graphSizes,
        visualKind: 'sparkline:thread_pressure',
        metricPriority: ['workers', 'idle', 'pressured', 'saturated', 'borrowed'],
        omitMetricsWhenVisual: ['pressure', 'queue'],
        requiresSeries: true,
      },
    },
  },
  {
    id: 'system.connections',
    sectionId: 'runtime',
    title: 'Connection Health',
    description: 'Websocket session mix and unauthenticated buildup.',
    href: '/dashboard/runtime#connections',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'hero', 'visual'],
    metricPriority: ['sessions', 'human', 'share', 'unauthenticated', 'oldest_session', 'oldest_unauth'],
    zeroLowValueMetricKeys: ['unauthenticated'],
    visualKind: 'stack:connections',
    omitMetricsWhenVisual: ['human', 'share', 'unauthenticated'],
    variants: commonVariants('stack:connections'),
  },
  {
    id: 'system.fuse',
    sectionId: 'filesystem',
    title: 'FUSE Filesystem',
    description: 'Live filesystem operation volume, errors, latency, and open handles.',
    href: '/dashboard/filesystem#fuse',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'visual', 'graph'],
    metricPriority: ['ops', 'total_errors', 'error_rate', 'open_handles', 'read_bytes', 'write_bytes'],
    visualKind: 'meter:fuse_error',
    omitMetricsWhenVisual: ['error_rate'],
    variants: {
      compact: { supportedSizes: ['2x1', '3x1'] },
      summary: { supportedSizes: summarySizes },
      visual: { supportedSizes: visualSizes, visualKind: 'meter:fuse_error' },
      graph: {
        supportedSizes: graphSizes,
        visualKind: 'sparkline:fuse_error',
        metricPriority: ['open_handles', 'total_errors', 'read_bytes', 'write_bytes'],
        omitMetricsWhenVisual: ['error_rate'],
        requiresSeries: true,
      },
    },
  },
  {
    id: 'system.fs_cache',
    sectionId: 'filesystem',
    title: 'FS Cache',
    description: 'Filesystem cache hit rate, usage, and churn.',
    href: '/dashboard/filesystem#fs-cache',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: ['1x1', '2x1', '3x1', '2x2', '3x2'],
    supportedVariants: ['compact', 'summary', 'visual', 'graph'],
    metricPriority: ['hit_rate', 'used', 'requests', 'misses', 'evictions'],
    zeroLowValueMetricKeys: ['evictions'],
    visualKind: 'meter:cache_hit',
    omitMetricsWhenVisual: ['hit_rate'],
    variants: {
      compact: { supportedSizes: ['1x1', '2x1', '3x1'] },
      summary: { supportedSizes: ['2x1', '3x1'] },
      visual: { supportedSizes: ['2x1', '2x2', '3x1', '3x2'], visualKind: 'meter:cache_hit' },
      graph: {
        supportedSizes: ['2x1', '2x2', '3x1', '3x2'],
        visualKind: 'sparkline:cache_hit',
        metricPriority: ['used', 'requests', 'misses', 'evictions'],
        omitMetricsWhenVisual: ['hit_rate'],
        requiresSeries: true,
      },
    },
  },
  {
    id: 'system.http_cache',
    sectionId: 'filesystem',
    title: 'HTTP Preview Cache',
    description: 'Preview cache hit rate, usage, and churn.',
    href: '/dashboard/filesystem#http-cache',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: ['1x1', '2x1', '3x1', '2x2', '3x2'],
    supportedVariants: ['compact', 'summary', 'visual', 'graph'],
    metricPriority: ['hit_rate', 'used', 'requests', 'misses', 'evictions'],
    zeroLowValueMetricKeys: ['evictions'],
    visualKind: 'meter:cache_hit',
    omitMetricsWhenVisual: ['hit_rate'],
    variants: {
      compact: { supportedSizes: ['1x1', '2x1', '3x1'] },
      summary: { supportedSizes: ['2x1', '3x1'] },
      visual: { supportedSizes: ['2x1', '2x2', '3x1', '3x2'], visualKind: 'meter:cache_hit' },
      graph: {
        supportedSizes: ['2x1', '2x2', '3x1', '3x2'],
        visualKind: 'sparkline:cache_hit',
        metricPriority: ['used', 'requests', 'misses', 'evictions'],
        omitMetricsWhenVisual: ['hit_rate'],
        requiresSeries: true,
      },
    },
  },
  {
    id: 'system.storage',
    sectionId: 'storage',
    title: 'Storage Backend',
    description: 'Local and S3 vault backend configuration and free-space posture.',
    href: '/dashboard/storage#storage-backend',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'hero', 'visual'],
    metricPriority: ['vaults', 'active', 'local', 's3', 'inactive', 'degraded', 'backend_errors'],
    zeroLowValueMetricKeys: ['inactive', 'backend_errors', 'degraded'],
    visualKind: 'stack:storage',
    omitMetricsWhenVisual: ['active', 'local', 's3', 'inactive'],
    variants: commonVariants('stack:storage'),
  },
  {
    id: 'system.db',
    sectionId: 'storage',
    title: 'Database Health',
    description: 'Database connectivity, connection pressure, cache hit ratio, and table size.',
    href: '/dashboard/storage#database',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'visual', 'graph'],
    metricPriority: ['cache_hit', 'connections', 'active_connections', 'size', 'slow_queries', 'oldest_tx'],
    zeroLowValueMetricKeys: ['slow_queries', 'oldest_tx'],
    visualKind: 'meter:db_cache',
    omitMetricsWhenVisual: ['cache_hit'],
    variants: {
      compact: { supportedSizes: ['2x1', '3x1'] },
      summary: { supportedSizes: summarySizes },
      visual: { supportedSizes: visualSizes, visualKind: 'meter:db_cache' },
      graph: {
        supportedSizes: graphSizes,
        visualKind: 'sparkline:db_cache',
        metricPriority: ['connections', 'active_connections', 'size', 'slow_queries', 'oldest_tx'],
        omitMetricsWhenVisual: ['cache_hit'],
        requiresSeries: true,
      },
    },
  },
  {
    id: 'system.retention',
    sectionId: 'storage',
    title: 'Retention / Cleanup',
    description: 'Trash, audit, sync, share, and cache cleanup backlog.',
    href: '/dashboard/storage#retention',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'hero', 'visual'],
    metricPriority: ['overdue', 'trash', 'trash_bytes', 'cache_expired', 'sync_backlog', 'audit_backlog'],
    zeroLowValueMetricKeys: ['cache_expired', 'sync_backlog', 'audit_backlog'],
    visualKind: 'stack:retention',
    omitMetricsWhenVisual: ['overdue', 'cache_expired', 'sync_backlog', 'audit_backlog'],
    variants: commonVariants('stack:retention'),
  },
  {
    id: 'system.operations',
    sectionId: 'operations',
    title: 'Operation Queue',
    description: 'Pending, active, failed, and stalled filesystem/share work.',
    href: '/dashboard/operations#operation-queue',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['compact', 'summary', 'hero', 'visual'],
    metricPriority: ['stalled', 'pending', 'in_progress', 'failed_24h', 'active_uploads', 'oldest_pending', 'oldest_active'],
    zeroLowValueMetricKeys: ['stalled', 'failed_24h', 'active_uploads'],
    visualKind: 'stack:operations',
    omitMetricsWhenVisual: ['pending', 'in_progress', 'stalled', 'failed_24h'],
    variants: commonVariants('stack:operations'),
  },
  {
    id: 'system.trends',
    sectionId: 'trends',
    title: 'Trends',
    description: 'Recently collected stats snapshot series.',
    href: '/dashboard/trends#trends',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: ['2x1', '2x2', '3x1', '3x2'],
    supportedVariants: ['compact', 'summary', 'visual', 'graph'],
    metricPriority: ['latest_sample_age', 'window', 'coverage'],
    lowValueMetricKeys: ['series', 'points'],
    visualKind: 'trend:coverage',
    omitMetricsWhenVisual: ['latest_sample_age', 'window', 'coverage'],
    variants: {
      compact: { supportedSizes: ['2x1', '3x1'], metricPriority: ['latest_sample_age', 'window'] },
      summary: { supportedSizes: ['2x1', '3x1'], metricPriority: ['latest_sample_age', 'window', 'coverage'] },
      visual: { supportedSizes: ['2x1', '2x2', '3x1', '3x2'], visualKind: 'trend:coverage' },
      graph: {
        supportedSizes: ['2x1', '2x2', '3x1', '3x2'],
        visualKind: 'sparkline:coverage',
        metricPriority: ['latest_sample_age', 'window', 'coverage'],
        omitMetricsWhenVisual: ['latest_sample_age', 'window', 'coverage', 'series', 'points'],
        requiresSeries: true,
      },
    },
  },
]

export const dashboardCardDefinitionsById = new Map(dashboardCardDefinitions.map(card => [card.id, card]))

const defaultVisibleCards: DashboardLayoutPresetDefinition['cards'] = [
  { id: 'system.operations', size: '2x1', variant: 'visual' },
  { id: 'system.storage', size: '2x1', variant: 'visual' },
  { id: 'system.threadpools', size: '2x1', variant: 'graph' },
  { id: 'system.fuse', size: '2x1', variant: 'visual' },
  { id: 'system.db', size: '2x1', variant: 'visual' },
  { id: 'system.retention', size: '2x1', variant: 'visual' },
  { id: 'system.connections', size: '2x1', variant: 'visual' },
  { id: 'system.fs_cache', size: '2x1', variant: 'visual' },
]

export const dashboardLayoutPresetDefinitions: DashboardLayoutPresetDefinition[] = [
  {
    id: 'default',
    title: 'Default',
    description: 'Balanced command-center view across runtime, operations, storage, filesystem, and cleanup posture.',
    cards: defaultVisibleCards,
  },
  {
    id: 'minimal',
    title: 'Minimal',
    description: 'Small health-first board for operators who mostly use drilldowns.',
    cards: [
      { id: 'system.operations', size: '2x1', variant: 'visual' },
      { id: 'system.storage', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
      { id: 'system.threadpools', size: '2x1', variant: 'graph' },
    ],
  },
  {
    id: 'runtime',
    title: 'Runtime',
    description: 'Runtime, workers, sessions, and filesystem pressure.',
    cards: [
      { id: 'system.threadpools', size: '2x1', variant: 'summary' },
      { id: 'system.threadpools', size: '3x1', variant: 'graph' },
      { id: 'system.connections', size: '2x1', variant: 'visual' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.operations', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
    ],
  },
  {
    id: 'storage',
    title: 'Storage',
    description: 'Backing providers, database, retention, and cache posture.',
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
    id: 'operations',
    title: 'Operations',
    description: 'Queued work, transfers, FUSE activity, and worker pressure.',
    cards: [
      { id: 'system.operations', size: '4x2', variant: 'hero' },
      { id: 'system.threadpools', size: '2x1', variant: 'graph' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.connections', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
    ],
  },
  {
    id: 'cockpit',
    title: 'Cockpit',
    description: 'Wide high-signal overview for wallboard-style monitoring.',
    cards: [
      { id: 'system.operations', size: '2x1', variant: 'visual' },
      { id: 'system.storage', size: '2x1', variant: 'visual' },
      { id: 'system.threadpools', size: '2x1', variant: 'summary' },
      { id: 'system.threadpools', size: '2x1', variant: 'graph' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
      { id: 'system.retention', size: '2x1', variant: 'visual' },
      { id: 'system.connections', size: '2x1', variant: 'visual' },
      { id: 'system.fs_cache', size: '2x1', variant: 'visual' },
      { id: 'system.http_cache', size: '2x1', variant: 'visual' },
      { id: 'system.trends', size: '2x1', variant: 'summary' },
    ],
  },
]

export function defaultDashboardLayoutDefinitionCards(): DashboardLayoutPresetDefinition['cards'] {
  return defaultVisibleCards
}
