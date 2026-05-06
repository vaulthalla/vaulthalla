'use client'

import type { DashboardIssueSummary } from '@/models/stats/dashboardOverview'
import { DashboardIssueList } from '@/components/dashboard/DashboardIssueList'
import { DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'

export function DashboardAttentionStrip({
  issues,
  hiddenCount,
}: {
  issues: DashboardIssueSummary[]
  hiddenCount: number
}) {
  if (!issues.length) {
    return (
      <div className="mt-4 inline-flex items-center gap-2 rounded-full border border-emerald-300/25 bg-emerald-400/10 px-3 py-1.5 text-xs text-emerald-100">
        <DashboardSeverityIcon severity="healthy" className="h-3.5 w-3.5 text-emerald-100" />
        No warning or error attention items
      </div>
    )
  }

  return (
    <div className="mt-4 rounded-2xl border border-white/10 bg-black/20 p-2.5">
      <div className="mb-2 flex flex-wrap items-center justify-between gap-2 text-xs">
        <span className="font-semibold text-white/80">Attention</span>
        {hiddenCount > 0 ?
          <span className="text-white/45">+{hiddenCount} more</span>
        : null}
      </div>
      <DashboardIssueList issues={issues} max={3} compact className="grid grid-cols-1 gap-2 lg:grid-cols-3" />
    </div>
  )
}
