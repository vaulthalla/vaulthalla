'use client'

import { keepPreviousData } from '@tanstack/react-query'
import { useWs } from '@/lib/query'
import { parseOverview, type Card, type Overview } from '@/features/health/model'
import type { DashboardOverviewRequest } from '@/models/stats/dashboardOverview'

export const OVERVIEW_POLL_MS = 10_000
export const DETAIL_POLL_MS = 15_000

const selectOverview = (data: { stats: unknown }): Overview => parseOverview(data.stats)

export const useOverview = (payload: DashboardOverviewRequest, refetchInterval = OVERVIEW_POLL_MS, enabled = true) =>
  useWs('stats.dashboard.overview', payload, {
    enabled,
    refetchInterval,
    staleTime: 5_000,
    // A layout change asks for a different card set; keep showing the last answer until the new one lands.
    placeholderData: keepPreviousData,
    select: selectOverview,
  })

// Backend severity and issues for a detail page's cards: a small "tiles" overview (no trend series).
export const useHealthCards = (ids: string[], refetchInterval = DETAIL_POLL_MS) => {
  const query = useWs(
    'stats.dashboard.overview',
    { scope: 'system', mode: 'detail', cards: ids.map(id => ({ id, variant: 'tiles', size: '2x1' })) },
    { refetchInterval, staleTime: 5_000, select: selectOverview },
  )
  const byId = new Map<string, Card>((query.data?.cards ?? []).map(card => [card.id, card]))
  return { cards: byId, pending: query.isPending, query }
}

export const detailPoll = { refetchInterval: DETAIL_POLL_MS, staleTime: 5_000 } as const
