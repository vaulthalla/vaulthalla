import Link from 'next/link'

import type {
  DashboardAttentionItem,
  DashboardIssueSummary,
  DashboardSeverity,
} from '@/models/stats/dashboardOverview'
import { DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardSeverityTone, sortDashboardIssues } from '@/components/dashboard/dashboardSeverity'

type DashboardIssue = DashboardIssueSummary | DashboardAttentionItem

function hasTitle(issue: DashboardIssue): issue is DashboardAttentionItem {
  return 'title' in issue
}

function issueDisplayKey(issue: DashboardIssue): string {
  const cardId = 'card_id' in issue ? issue.card_id : ''
  return `${issue.severity}:${cardId}:${issue.code}:${issue.message}`
}

export function dedupeDashboardIssues<T extends DashboardIssue>(issues: T[]): T[] {
  const seen = new Set<string>()
  return issues.filter(issue => {
    const key = issueDisplayKey(issue)
    if (seen.has(key)) return false
    seen.add(key)
    return true
  })
}

export function DashboardIssueRow({
  issue,
  link = true,
  compact = false,
}: {
  issue: DashboardIssue
  link?: boolean
  compact?: boolean
}) {
  const tone = dashboardSeverityTone(issue.severity)
  const content = (
    <div
      className={[
        'flex items-start gap-3 rounded-2xl border',
        compact ? 'px-3 py-2' : 'px-3.5 py-3',
        tone.border,
        tone.bg,
        issue.severity === 'error' || issue.severity === 'warning' ? tone.glow : '',
      ].join(' ')}>
      <span
        className={[
          'inline-flex shrink-0 items-center justify-center rounded-full border',
          compact ? 'mt-0.5 h-6 w-6' : 'mt-0.5 h-7 w-7',
          tone.border,
          tone.bg,
          tone.text,
        ].join(' ')}>
        <DashboardSeverityIcon severity={issue.severity} className={compact ? 'h-3.5 w-3.5' : 'h-4 w-4'} />
      </span>
      <div className="min-w-0">
        {hasTitle(issue) ?
          <div className="mb-0.5 text-xs font-semibold text-white/70">{issue.title}</div>
        : null}
        <div className={['text-white/85', compact ? 'text-xs' : 'text-sm'].join(' ')}>{issue.message}</div>
      </div>
    </div>
  )

  return link && issue.href ?
      <Link href={issue.href} className="block transition hover:brightness-125">
        {content}
      </Link>
    : content
}

export function DashboardIssueList({
  issues,
  max = 2,
  link = true,
  compact = false,
  className = 'space-y-2',
}: {
  issues: DashboardIssue[]
  max?: number
  link?: boolean
  compact?: boolean
  className?: string
}) {
  const visible = dedupeDashboardIssues(sortDashboardIssues(issues)).slice(0, max)
  if (!visible.length) return null

  return (
    <div className={className}>
      {visible.map((issue, index) => (
        <DashboardIssueRow
          key={`${issueDisplayKey(issue)}:${index}`}
          issue={issue}
          link={link}
          compact={compact}
        />
      ))}
    </div>
  )
}

export function DashboardIssueSummaryLine({
  severity,
  errorCount,
  warningCount,
  firstIssue,
}: {
  severity: DashboardSeverity
  errorCount: number
  warningCount: number
  firstIssue?: DashboardIssue | null
}) {
  const tone = dashboardSeverityTone(severity)
  const total = errorCount + warningCount
  const countText =
    errorCount > 0 ? `${errorCount} error${errorCount === 1 ? '' : 's'}`
    : warningCount > 0 ? `${warningCount} warning${warningCount === 1 ? '' : 's'}`
    : null

  if (!countText && !firstIssue) return null

  return (
    <div className={['mt-3 flex items-start gap-2 text-xs', tone.soft].join(' ')}>
      <DashboardSeverityIcon severity={severity} className="mt-0.5 h-3.5 w-3.5" />
      <span className="min-w-0">
        {countText ?
          <span className="font-semibold">{countText}</span>
        : null}
        {countText && firstIssue ?
          <span className="text-white/35"> · </span>
        : null}
        {firstIssue ?
          <span className="text-white/70">{firstIssue.message}</span>
        : total > 0 ?
          null
        : null}
      </span>
    </div>
  )
}
