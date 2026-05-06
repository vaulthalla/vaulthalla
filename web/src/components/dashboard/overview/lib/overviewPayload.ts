import { visibleDashboardLayoutCards, type DashboardLayoutCard } from '@/models/dashboard/dashboardLayout'
import type { DashboardOverviewRequest } from '@/models/stats/dashboardOverview'

export const SYSTEM_HEALTH_CARD_ID = 'system.health'

export function buildOverviewPayload(layout: DashboardLayoutCard[]): DashboardOverviewRequest {
  return {
    scope: 'system',
    mode: 'dashboard_home',
    cards: [
      { id: SYSTEM_HEALTH_CARD_ID, size: '3x2', variant: 'hero' },
      ...visibleDashboardLayoutCards(layout)
        .filter(card => card.id !== SYSTEM_HEALTH_CARD_ID)
        .map(card => ({
          id: card.id,
          size: card.size,
          variant: card.variant,
        })),
    ],
  }
}
