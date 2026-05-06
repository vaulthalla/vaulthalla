import type { DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'
import { DashboardCardSummary, DashboardMetricSummary } from '@/models/stats/dashboardOverview'

type VariantPreference = Partial<Record<DashboardLayoutCard['variant'], string[]>>

const basePreferences: Record<string, string[]> = {
  'system.health': ['services', 'protocols', 'deps', 'sessions'],
  'system.threadpools': ['queue', 'pressure', 'workers', 'idle', 'pressured', 'saturated', 'borrowed'],
  'system.connections': ['sessions', 'human', 'share', 'unauthenticated', 'oldest_session', 'oldest_unauth'],
  'system.fuse': ['ops', 'total_errors', 'error_rate', 'open_handles', 'read_bytes', 'write_bytes'],
  'system.fs_cache': ['hit_rate', 'used', 'requests', 'misses', 'evictions'],
  'system.http_cache': ['hit_rate', 'used', 'requests', 'misses', 'evictions'],
  'system.storage': ['vaults', 'active', 'local', 's3', 'inactive', 'degraded', 'backend_errors'],
  'system.db': ['cache_hit', 'connections', 'active_connections', 'size', 'slow_queries', 'oldest_tx'],
  'system.retention': ['overdue', 'trash', 'trash_bytes', 'cache_expired', 'sync_backlog', 'audit_backlog'],
  'system.operations': ['stalled', 'pending', 'in_progress', 'failed_24h', 'active_uploads', 'oldest_pending', 'oldest_active'],
  'system.trends': ['latest_sample_age', 'window', 'coverage'],
}

const variantPreferences: Record<string, VariantPreference> = {
  'system.health': {
    compact: ['services', 'protocols', 'deps'],
    hero: ['services', 'protocols', 'deps', 'sessions'],
  },
  'system.trends': {
    compact: ['latest_sample_age', 'window'],
    visual: ['latest_sample_age', 'window', 'coverage'],
  },
}

const lowValueKeys: Record<string, Set<string>> = {
  'system.trends': new Set(['series', 'points']),
}

const visualMetricKeys: Record<string, Set<string>> = {
  'system.threadpools': new Set(['pressure']),
  'system.connections': new Set(['human', 'share', 'unauthenticated']),
  'system.fuse': new Set(['error_rate']),
  'system.fs_cache': new Set(['hit_rate']),
  'system.http_cache': new Set(['hit_rate']),
  'system.storage': new Set(['active', 'local', 's3', 'inactive']),
  'system.db': new Set(['cache_hit']),
  'system.retention': new Set(['overdue', 'cache_expired', 'sync_backlog', 'audit_backlog']),
  'system.operations': new Set(['pending', 'in_progress', 'stalled', 'failed_24h']),
  'system.trends': new Set(['latest_sample_age', 'window', 'coverage']),
}

export function dashboardMetricNumber(metric: DashboardMetricSummary): number | null {
  if (metric.numeric_value !== null && Number.isFinite(metric.numeric_value)) return metric.numeric_value

  const parsed = Number(metric.value.replace(/,/g, '').replace(/%$/, ''))
  return Number.isFinite(parsed) ? parsed : null
}

export function dashboardMetricByKey(metrics: DashboardMetricSummary[]): Map<string, DashboardMetricSummary> {
  return new Map(metrics.map(metric => [metric.key, metric]))
}

export function isLowValueDashboardMetric(cardId: string, metric: DashboardMetricSummary): boolean {
  if (lowValueKeys[cardId]?.has(metric.key)) return true

  const value = dashboardMetricNumber(metric)
  if ((cardId === 'system.fs_cache' || cardId === 'system.http_cache') && metric.key === 'evictions' && value === 0) return true
  if (cardId === 'system.connections' && metric.key === 'unauthenticated' && value === 0) return true
  if (cardId === 'system.storage' && metric.key === 'inactive' && value === 0) return true
  if (cardId === 'system.storage' && metric.key === 'backend_errors' && value === 0) return true
  if (cardId === 'system.storage' && metric.key === 'degraded' && value === 0) return true
  if (cardId === 'system.db' && (metric.key === 'slow_queries' || metric.key === 'oldest_tx') && value === 0) return true
  if (cardId === 'system.retention' && (metric.key === 'cache_expired' || metric.key === 'sync_backlog' || metric.key === 'audit_backlog') && value === 0) return true
  if (cardId === 'system.operations' && (metric.key === 'stalled' || metric.key === 'failed_24h' || metric.key === 'active_uploads') && value === 0) return true

  return false
}

export function dashboardVisualMetricKeys(cardId: string): Set<string> {
  return visualMetricKeys[cardId] ?? new Set<string>()
}

export function selectDashboardCardMetrics(
  card: DashboardCardSummary,
  layoutCard: DashboardLayoutCard,
  count: number,
  omitKeys: Set<string> = new Set(),
): DashboardMetricSummary[] {
  const preferred = variantPreferences[card.id]?.[layoutCard.variant] ?? basePreferences[card.id] ?? []
  const byKey = dashboardMetricByKey(card.metrics)
  const selected: DashboardMetricSummary[] = []
  const selectedKeys = new Set<string>()

  for (const key of preferred) {
    const metric = byKey.get(key)
    if (omitKeys.has(key)) continue
    if (!metric || isLowValueDashboardMetric(card.id, metric)) continue
    selected.push(metric)
    selectedKeys.add(metric.key)
  }

  const compact = layoutCard.variant === 'compact' || layoutCard.size === '1x1' || layoutCard.size === '2x1'
  for (const metric of card.metrics) {
    if (omitKeys.has(metric.key)) continue
    if (selectedKeys.has(metric.key)) continue
    if ((compact || card.id === 'system.trends') && isLowValueDashboardMetric(card.id, metric)) continue
    selected.push(metric)
    selectedKeys.add(metric.key)
  }

  return selected.slice(0, count)
}

export function dashboardMetricMeterValue(metric: DashboardMetricSummary): number | null {
  const value = metric.numeric_value
  if (value === null || !Number.isFinite(value)) return null

  if (metric.key === 'hit_rate' || metric.key === 'cache_hit' || metric.key === 'error_rate') {
    return Math.max(0, Math.min(1, value))
  }

  if (metric.key === 'pressure') return Math.max(0, Math.min(1, value / 8))

  return null
}
