import type { DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'
import { DashboardCardSummary, DashboardMetricSummary } from '@/models/stats/dashboardOverview'
import {
  dashboardCardDefinitionsById,
  type DashboardCardDefinition,
} from '@/components/dashboard/dashboardCardDefinitions'

export function dashboardMetricNumber(metric: DashboardMetricSummary): number | null {
  if (metric.numeric_value !== null && Number.isFinite(metric.numeric_value)) return metric.numeric_value

  const parsed = Number(metric.value.replace(/,/g, '').replace(/%$/, ''))
  return Number.isFinite(parsed) ? parsed : null
}

export function dashboardMetricByKey(metrics: DashboardMetricSummary[]): Map<string, DashboardMetricSummary> {
  return new Map(metrics.map(metric => [metric.key, metric]))
}

export function dashboardDefinitionForCard(cardId: string): DashboardCardDefinition | null {
  return dashboardCardDefinitionsById.get(cardId) ?? null
}

export function dashboardMetricPriority(cardId: string, variant: DashboardLayoutCard['variant']): string[] {
  const definition = dashboardDefinitionForCard(cardId)
  return definition?.variants[variant]?.metricPriority ?? definition?.metricPriority ?? []
}

export function dashboardVisualMetricKeys(cardId: string, variant?: DashboardLayoutCard['variant']): Set<string> {
  const definition = dashboardDefinitionForCard(cardId)
  if (!definition) return new Set<string>()
  const variantDefinition = variant ? definition.variants[variant] : null
  return new Set(variantDefinition?.omitMetricsWhenVisual ?? definition.omitMetricsWhenVisual ?? [])
}

export function isLowValueDashboardMetric(cardId: string, metric: DashboardMetricSummary): boolean {
  const definition = dashboardDefinitionForCard(cardId)
  if (!definition) return false
  if (definition.lowValueMetricKeys?.includes(metric.key)) return true

  const value = dashboardMetricNumber(metric)
  return Boolean(definition.zeroLowValueMetricKeys?.includes(metric.key) && value === 0)
}

export function selectDashboardCardMetrics(
  card: DashboardCardSummary,
  layoutCard: DashboardLayoutCard,
  count: number,
  omitKeys: Set<string> = new Set(),
): DashboardMetricSummary[] {
  const preferred = dashboardMetricPriority(card.id, layoutCard.variant)
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

  for (const metric of card.metrics) {
    if (omitKeys.has(metric.key)) continue
    if (selectedKeys.has(metric.key)) continue
    if (isLowValueDashboardMetric(card.id, metric)) continue
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
