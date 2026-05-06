import type { ComponentType, SVGProps } from 'react'

import type { DashboardSeverity } from '@/models/stats/dashboardOverview'
import {
  dashboardIssueCount,
  dashboardIssueCountLabel,
  dashboardSeverityTone,
} from '@/components/dashboard/dashboardSeverity'
import BanIcon from '@/fa-duotone/ban.svg'
import CheckIcon from '@/fa-duotone/circle-check.svg'
import ErrorIcon from '@/fa-duotone/circle-xmark.svg'
import InfoIcon from '@/fa-duotone/circle-info.svg'
import QuestionIcon from '@/fa-duotone/circle-question.svg'
import WarningIcon from '@/fa-duotone/triangle-exclamation.svg'

const severityIcons: Record<DashboardSeverity, ComponentType<SVGProps<SVGSVGElement>>> = {
  healthy: CheckIcon,
  info: InfoIcon,
  warning: WarningIcon,
  error: ErrorIcon,
  unknown: QuestionIcon,
  unavailable: BanIcon,
}

export function DashboardSeverityIcon({
  severity,
  className = 'h-4 w-4',
}: {
  severity: DashboardSeverity
  className?: string
}) {
  const Icon = severityIcons[severity] ?? QuestionIcon
  return <Icon className={['shrink-0 fill-current', className].join(' ')} aria-hidden="true" />
}

export function DashboardSeverityBadge({
  severity,
  errorCount = 0,
  warningCount = 0,
  showCount = false,
  compact = false,
}: {
  severity: DashboardSeverity
  errorCount?: number
  warningCount?: number
  showCount?: boolean
  compact?: boolean
}) {
  const tone = dashboardSeverityTone(severity)
  const count = dashboardIssueCount(errorCount, warningCount)
  const countLabel = dashboardIssueCountLabel(errorCount, warningCount)

  if (compact) {
    return (
      <span
        className={[
          'inline-flex items-center gap-1 rounded-full border px-1.5 py-1 text-[10px] leading-none',
          tone.border,
          tone.bg,
          tone.text,
          tone.glow,
        ].join(' ')}
        aria-label={countLabel ?? tone.label}>
        <DashboardSeverityIcon severity={severity} className="h-3 w-3" />
        {showCount && count > 0 ?
          <span className="min-w-3 text-center font-semibold">{count}</span>
        : null}
      </span>
    )
  }

  return (
    <span
      className={[
        'inline-flex items-center gap-2 rounded-full border px-2.5 py-1 text-xs',
        tone.border,
        tone.bg,
        tone.text,
        tone.glow,
      ].join(' ')}
      title={countLabel ?? tone.label}>
      <DashboardSeverityIcon severity={severity} />
      <span>{tone.label}</span>
      {showCount && count > 0 ?
        <>
          <span className="text-white/30">·</span>
          <span className="font-semibold">{count}</span>
        </>
      : null}
    </span>
  )
}

export function DashboardIssueCountBadge({
  severity,
  errorCount,
  warningCount,
  compact = false,
}: {
  severity: DashboardSeverity
  errorCount: number
  warningCount: number
  compact?: boolean
}) {
  const count = dashboardIssueCount(errorCount, warningCount)
  if (count <= 0) return null

  return (
    <DashboardSeverityBadge
      severity={severity}
      errorCount={errorCount}
      warningCount={warningCount}
      showCount
      compact={compact}
    />
  )
}
