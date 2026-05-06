'use client'

import Link from 'next/link'

import type { DashboardMetricSummary } from '@/models/stats/dashboardOverview'
import { dashboardSeverityTone } from '@/components/dashboard/dashboardSeverity'

export function DashboardMetricTile({ metric, dense = false }: { metric: DashboardMetricSummary; dense?: boolean }) {
  const tone = dashboardSeverityTone(metric.tone)
  const body = (
    <div
      className={[
        'flex min-w-0 flex-col justify-center rounded-lg border',
        dense ? 'h-11' : 'h-12',
        dense ? 'px-2 py-1' : 'px-2.5 py-1.5',
        tone.border,
        tone.bg,
      ].join(' ')}>
      <div className="truncate text-[8.5px] leading-none tracking-[0.06em] text-white/45 uppercase">{metric.label}</div>
      <div
        className={[
          'mt-1 truncate font-semibold leading-none',
          dense ? 'text-[14px]' : 'text-[16px]',
          tone.text,
        ].join(' ')}>
        {metric.value || 'unknown'}
      </div>
    </div>
  )

  return metric.href ?
      <Link href={metric.href} className="transition hover:brightness-125">
        {body}
      </Link>
    : body
}
