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
  | 'sparkline:operations'
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

const y1AndY2Sizes: DashboardCardSize[] = ['1x1', '1x2', '2x1', '2x2', '3x1', '3x2', '4x2']
const tilesSizes: DashboardCardSize[] = ['2x1', '3x1', '1x1']
const wideTilesSizes: DashboardCardSize[] = ['2x1', '3x1']
const visualSizes: DashboardCardSize[] = ['1x2', '2x1', '2x2', '3x1', '3x2', '4x2']

function commonVariants(
  visualKind: DashboardCardVisualKind,
  tileSupportedSizes: DashboardCardSize[] = tilesSizes,
  visualSupportedSizes: DashboardCardSize[] = visualSizes,
): Partial<Record<DashboardCardVariant, DashboardCardVariantDefinition>> {
  return {
    tiles: { supportedSizes: tileSupportedSizes },
    visual: { supportedSizes: visualSupportedSizes, visualKind },
  }
}

export const dashboardCardDefinitions: DashboardCardDefinition[] = [
  {
    id: 'system.threadpools',
    sectionId: 'runtime',
    title: 'Thread Pools',
    description: 'Runtime worker pressure across FUSE, sync, thumbnails, HTTP, and stats.',
    href: '/dashboard/runtime#thread-pools',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['pressure', 'queue', 'saturated', 'pressured', 'workers', 'busy', 'idle', 'borrowed', 'pools', 'stopped', 'degraded'],
    zeroLowValueMetricKeys: ['queue', 'saturated', 'pressured', 'busy', 'borrowed', 'stopped', 'degraded'],
    visualKind: 'sparkline:thread_pressure',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: tilesSizes },
      visual: {
        supportedSizes: visualSizes,
        visualKind: 'sparkline:thread_pressure',
        metricPriority: ['pressure', 'queue', 'saturated', 'pressured', 'workers', 'busy', 'idle', 'borrowed', 'pools', 'stopped', 'degraded'],
        omitMetricsWhenVisual: [],
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
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['unauthenticated', 'share_pending', 'sessions', 'human', 'share', 'oldest_unauth', 'oldest_session', 'errors_24h', 'opened_24h', 'closed_24h', 'swept_24h', 'idle_timeout', 'unauth_timeout', 'sweep_interval'],
    zeroLowValueMetricKeys: ['unauthenticated', 'share_pending', 'oldest_unauth', 'oldest_session', 'errors_24h', 'opened_24h', 'closed_24h', 'swept_24h'],
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
    supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2', '4x2'],
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['error_rate', 'total_errors', 'ops', 'successes', 'open_handles', 'open_peak', 'avg_latency', 'max_latency', 'read_bytes', 'write_bytes', 'op_types', 'errno_types'],
    zeroLowValueMetricKeys: ['total_errors', 'open_handles', 'open_peak', 'avg_latency', 'max_latency', 'read_bytes', 'write_bytes', 'op_types', 'errno_types'],
    visualKind: 'sparkline:fuse_error',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: wideTilesSizes },
      visual: {
        supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2', '4x2'],
        visualKind: 'sparkline:fuse_error',
        metricPriority: ['error_rate', 'total_errors', 'ops', 'successes', 'open_handles', 'open_peak', 'avg_latency', 'max_latency', 'read_bytes', 'write_bytes', 'op_types', 'errno_types'],
        omitMetricsWhenVisual: [],
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
    supportedSizes: ['1x1', '1x2', '2x1', '2x2', '3x1', '3x2'],
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['hit_rate', 'occupancy', 'requests', 'used', 'free', 'capacity', 'misses', 'hits', 'inserts', 'evictions', 'invalidations', 'read_bytes', 'write_bytes', 'work_ops', 'avg_op', 'max_op'],
    zeroLowValueMetricKeys: ['requests', 'misses', 'hits', 'inserts', 'evictions', 'invalidations', 'read_bytes', 'write_bytes', 'work_ops', 'avg_op', 'max_op'],
    visualKind: 'sparkline:cache_hit',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: tilesSizes },
      visual: {
        supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2'],
        visualKind: 'sparkline:cache_hit',
        metricPriority: ['hit_rate', 'occupancy', 'requests', 'used', 'free', 'capacity', 'misses', 'hits', 'inserts', 'evictions', 'invalidations', 'read_bytes', 'write_bytes', 'work_ops', 'avg_op', 'max_op'],
        omitMetricsWhenVisual: [],
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
    supportedSizes: ['1x1', '1x2', '2x1', '2x2', '3x1', '3x2'],
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['hit_rate', 'occupancy', 'requests', 'used', 'free', 'capacity', 'misses', 'hits', 'inserts', 'evictions', 'invalidations', 'read_bytes', 'write_bytes', 'work_ops', 'avg_op', 'max_op'],
    zeroLowValueMetricKeys: ['requests', 'misses', 'hits', 'inserts', 'evictions', 'invalidations', 'read_bytes', 'write_bytes', 'work_ops', 'avg_op', 'max_op'],
    visualKind: 'sparkline:cache_hit',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: tilesSizes },
      visual: {
        supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2'],
        visualKind: 'sparkline:cache_hit',
        metricPriority: ['hit_rate', 'occupancy', 'requests', 'used', 'free', 'capacity', 'misses', 'hits', 'inserts', 'evictions', 'invalidations', 'read_bytes', 'write_bytes', 'work_ops', 'avg_op', 'max_op'],
        omitMetricsWhenVisual: [],
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
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['backend_errors', 'degraded', 'problem', 'inactive', 'vaults', 'healthy', 'active', 'local', 's3', 'providers'],
    zeroLowValueMetricKeys: ['backend_errors', 'degraded', 'problem', 'inactive', 'local', 's3', 'providers'],
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
    supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2', '4x2'],
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['cache_hit', 'connections', 'active_connections', 'idle_tx_connections', 'slow_queries', 'deadlocks', 'temp_bytes', 'oldest_tx', 'size', 'max_connections', 'idle_connections', 'largest_tables'],
    zeroLowValueMetricKeys: ['idle_tx_connections', 'max_connections', 'slow_queries', 'deadlocks', 'temp_bytes', 'oldest_tx', 'largest_tables'],
    visualKind: 'sparkline:db_cache',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: wideTilesSizes },
      visual: {
        supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2', '4x2'],
        visualKind: 'sparkline:db_cache',
        metricPriority: ['cache_hit', 'connections', 'active_connections', 'idle_tx_connections', 'slow_queries', 'deadlocks', 'temp_bytes', 'oldest_tx', 'size', 'max_connections', 'idle_connections', 'largest_tables'],
        omitMetricsWhenVisual: [],
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
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['overdue', 'sync_backlog', 'audit_backlog', 'cache_expired', 'trash', 'trash_bytes', 'overdue_bytes', 'cache_candidates', 'cache_entries', 'cache_bytes', 'sync_events', 'audit_events', 'share_events', 'oldest_trash', 'trash_retention', 'cache_expiry'],
    zeroLowValueMetricKeys: ['overdue', 'sync_backlog', 'audit_backlog', 'cache_expired', 'overdue_bytes', 'cache_candidates', 'cache_entries', 'cache_bytes'],
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
    defaultSize: '2x2',
    supportedSizes: y1AndY2Sizes,
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['stalled', 'pending', 'in_progress', 'failed_24h', 'cancelled_24h', 'active_uploads', 'stalled_uploads', 'failed_uploads_24h', 'upload_progress', 'upload_received', 'upload_expected', 'oldest_pending', 'oldest_active', 'oldest_upload', 'move_ops', 'copy_ops', 'rename_ops', 'success_ops', 'error_ops'],
    zeroLowValueMetricKeys: ['stalled', 'failed_24h', 'cancelled_24h', 'active_uploads', 'stalled_uploads', 'failed_uploads_24h', 'upload_progress', 'upload_received', 'upload_expected', 'oldest_pending', 'oldest_active', 'oldest_upload', 'move_ops', 'copy_ops', 'rename_ops', 'success_ops', 'error_ops'],
    visualKind: 'sparkline:operations',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: tilesSizes },
      visual: {
        supportedSizes: visualSizes,
        visualKind: 'sparkline:operations',
        metricPriority: ['pending', 'in_progress', 'stalled', 'failed_24h', 'cancelled_24h', 'active_uploads', 'stalled_uploads', 'failed_uploads_24h', 'upload_progress', 'upload_received', 'upload_expected', 'oldest_pending', 'oldest_active', 'oldest_upload', 'move_ops', 'copy_ops', 'rename_ops', 'success_ops', 'error_ops'],
        omitMetricsWhenVisual: [],
        requiresSeries: true,
      },
    },
  },
  {
    id: 'system.trends',
    sectionId: 'trends',
    title: 'Trends',
    description: 'Recently collected stats snapshot series.',
    href: '/dashboard/trends#trends',
    defaultVariant: 'visual',
    defaultSize: '2x1',
    supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2'],
    supportedVariants: ['tiles', 'visual'],
    metricPriority: ['latest_sample_age', 'coverage', 'series', 'points', 'threadpool_series', 'fuse_series', 'cache_series', 'db_series', 'operation_series', 'window'],
    lowValueMetricKeys: ['series', 'points'],
    zeroLowValueMetricKeys: ['latest_sample_age', 'coverage', 'threadpool_series', 'fuse_series', 'cache_series', 'db_series', 'operation_series'],
    visualKind: 'sparkline:coverage',
    omitMetricsWhenVisual: [],
    variants: {
      tiles: { supportedSizes: wideTilesSizes, metricPriority: ['latest_sample_age', 'coverage', 'series', 'points', 'threadpool_series', 'fuse_series', 'cache_series', 'db_series', 'operation_series', 'window'] },
      visual: {
        supportedSizes: ['1x2', '2x1', '2x2', '3x1', '3x2'],
        visualKind: 'sparkline:coverage',
        metricPriority: ['latest_sample_age', 'coverage', 'series', 'points', 'threadpool_series', 'fuse_series', 'cache_series', 'db_series', 'operation_series', 'window'],
        omitMetricsWhenVisual: [],
        requiresSeries: true,
      },
    },
  },
]

export const dashboardCardDefinitionsById = new Map(dashboardCardDefinitions.map(card => [card.id, card]))

const defaultVisibleCards: DashboardLayoutPresetDefinition['cards'] = [
  { id: 'system.operations', size: '2x2', variant: 'visual' },
  { id: 'system.storage', size: '2x1', variant: 'visual' },
  { id: 'system.threadpools', size: '2x1', variant: 'visual' },
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
      { id: 'system.operations', size: '2x1', variant: 'tiles' },
      { id: 'system.storage', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
      { id: 'system.threadpools', size: '2x1', variant: 'visual' },
    ],
  },
  {
    id: 'runtime',
    title: 'Runtime',
    description: 'Runtime, workers, sessions, and filesystem pressure.',
    cards: [
      { id: 'system.threadpools', size: '2x1', variant: 'tiles' },
      { id: 'system.threadpools', size: '3x1', variant: 'visual' },
      { id: 'system.connections', size: '2x1', variant: 'visual' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.operations', size: '2x2', variant: 'visual' },
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
      { id: 'system.operations', size: '4x2', variant: 'visual' },
      { id: 'system.threadpools', size: '2x1', variant: 'visual' },
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
      { id: 'system.operations', size: '2x2', variant: 'visual' },
      { id: 'system.storage', size: '2x1', variant: 'visual' },
      { id: 'system.threadpools', size: '2x1', variant: 'tiles' },
      { id: 'system.threadpools', size: '2x1', variant: 'visual' },
      { id: 'system.fuse', size: '2x1', variant: 'visual' },
      { id: 'system.db', size: '2x1', variant: 'visual' },
      { id: 'system.retention', size: '2x1', variant: 'visual' },
      { id: 'system.connections', size: '2x1', variant: 'visual' },
      { id: 'system.fs_cache', size: '2x1', variant: 'visual' },
      { id: 'system.http_cache', size: '2x1', variant: 'visual' },
      { id: 'system.trends', size: '2x1', variant: 'visual' },
    ],
  },
]

export function defaultDashboardLayoutDefinitionCards(): DashboardLayoutPresetDefinition['cards'] {
  return defaultVisibleCards
}
