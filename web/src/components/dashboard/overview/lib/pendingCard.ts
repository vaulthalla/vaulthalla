import type { DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'
import { DashboardCardSummary } from '@/models/stats/dashboardOverview'
import type { DashboardCardCatalogItem } from '@/components/dashboard/dashboardCardCatalog'

export function pendingCardForLayout(layoutCard: DashboardLayoutCard, catalogItem: DashboardCardCatalogItem): DashboardCardSummary {
  return new DashboardCardSummary({
    id: catalogItem.id,
    section_id: catalogItem.sectionId,
    title: catalogItem.title,
    description: catalogItem.description,
    href: catalogItem.href,
    variant: layoutCard.variant,
    size: layoutCard.size,
    severity: 'unknown',
    available: true,
    summary: 'Waiting for backend summary.',
    metrics: [],
    warnings: [],
    errors: [],
    checked_at: null,
  })
}
