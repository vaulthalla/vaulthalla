import type { DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'
import { DashboardCardSummary, DashboardMetricSummary } from '@/models/stats/dashboardOverview'

type VariantPreference = Partial<Record<DashboardLayoutCard['variant'], string[]>>

const basePreferences: Record<string, string[]> = {
  'system.health': ['services', 'protocols', 'deps', 'sessions'],
  'system.threadpools': ['queue', 'pressure', 'workers', 'pressured', 'saturated'],
  'system.connections': ['sessions', 'human', 'share', 'unauthenticated'],
  'system.fuse': ['error_rate', 'ops', 'open_handles'],
  'system.fs_cache': ['hit_rate', 'used', 'evictions'],
  'system.http_cache': ['hit_rate', 'used', 'evictions'],
  'system.storage': ['vaults', 'active', 'local', 's3'],
  'system.db': ['cache_hit', 'connections', 'size'],
  'system.retention': ['overdue', 'trash', 'cache_expired'],
  'system.operations': ['stalled', 'pending', 'in_progress', 'failed_24h'],
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

  return false
}

export function selectDashboardCardMetrics(
  card: DashboardCardSummary,
  layoutCard: DashboardLayoutCard,
  count: number,
): DashboardMetricSummary[] {
  const preferred = variantPreferences[card.id]?.[layoutCard.variant] ?? basePreferences[card.id] ?? []
  const byKey = dashboardMetricByKey(card.metrics)
  const selected: DashboardMetricSummary[] = []
  const selectedKeys = new Set<string>()

  for (const key of preferred) {
    const metric = byKey.get(key)
    if (!metric || isLowValueDashboardMetric(card.id, metric)) continue
    selected.push(metric)
    selectedKeys.add(metric.key)
  }

  const compact = layoutCard.variant === 'compact' || layoutCard.size === '1x1' || layoutCard.size === '2x1'
  for (const metric of card.metrics) {
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
