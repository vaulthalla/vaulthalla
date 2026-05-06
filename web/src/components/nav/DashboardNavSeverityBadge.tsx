'use client'

import { useEffect } from 'react'

import { DashboardSeverityBadge } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardIssueCount, dashboardSeverityTone } from '@/components/dashboard/dashboardSeverity'
import type { DashboardNavSeveritySource } from '@/components/nav/types'
import { useStatsStore } from '@/stores/statsStore'

function useDashboardNavSeverity(source: DashboardNavSeveritySource) {
  const overview = useStatsStore(s => s.dashboardOverview.data)
  const lastUpdated = useStatsStore(s => s.dashboardOverview.lastUpdated)
  const loading = useStatsStore(s => s.dashboardOverview.loading)
  const error = useStatsStore(s => s.dashboardOverview.error)
  const startPolling = useStatsStore(s => s.startDashboardOverviewPolling)

  useEffect(() => {
    startPolling(15000)
  }, [startPolling])

  if (!lastUpdated && !loading && !error) return null

  if (source.kind === 'overall') {
    return {
      severity: error ? 'error' as const : overview.overall_status,
      errorCount: error ? 1 : overview.error_count,
      warningCount: error ? 0 : overview.warning_count,
    }
  }

  const section = overview.sections.find(item => item.id === source.sectionId)
  if (!section) {
    return {
      severity: error ? 'error' as const : 'unknown' as const,
      errorCount: error ? 1 : 0,
      warningCount: 0,
    }
  }

  return {
    severity: section.severity,
    errorCount: section.error_count,
    warningCount: section.warning_count,
  }
}

export function DashboardNavSeverityBadge({
  source,
  compact = false,
}: {
  source: DashboardNavSeveritySource
  compact?: boolean
}) {
  const state = useDashboardNavSeverity(source)
  if (!state) return null

  const count = dashboardIssueCount(state.errorCount, state.warningCount)

  if (compact) {
    const tone = dashboardSeverityTone(state.severity)
    return (
      <span
        className={[
          'absolute -top-1 -right-1 inline-flex min-h-4 min-w-4 items-center justify-center rounded-full border px-1 text-[10px] leading-none font-semibold',
          tone.border,
          tone.bg,
          tone.text,
          tone.glow,
        ].join(' ')}>
        {count > 0 ? count : <span className={['h-1.5 w-1.5 rounded-full', tone.dot].join(' ')} />}
      </span>
    )
  }

  return (
    <DashboardSeverityBadge
      severity={state.severity}
      errorCount={state.errorCount}
      warningCount={state.warningCount}
      showCount={count > 0}
      compact
    />
  )
}
