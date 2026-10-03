'use client'

// The one set of health building blocks every health page uses: live status, backend severity badges, issue
// lists, section panels and boolean facts. Tones come only from backend severities (see model.ts).

import React from 'react'
import Link from 'next/link'
import type { UseQueryResult } from '@tanstack/react-query'
import { cn } from '@/util/cn'
import { Badge, SeverityBadge } from '@/components/ui/Badge'
import { Panel } from '@/components/ui/Panel'
import { ErrorState, Skeleton } from '@/components/ui/State'
import { StatTile } from '@/components/ui/Stat'
import { CircleCheckIcon, CircleExclamationIcon, CircleInfoIcon, ChevronRightIcon, TriangleExclamationIcon } from '@/components/ui/icons'
import { formatTime } from '@/lib/format'
import { severityTone, toneClasses, type Tone } from '@/lib/tone'
import { errorMessage } from '@/lib/ws/errors'
import {
  healthHref,
  issueCountText,
  metricDisplay,
  metricTone,
  severityText,
  sortIssues,
  type Card,
  type Issue,
  type Metric,
  type Severity,
} from '@/features/health/model'

type LiveQuery = Pick<UseQueryResult<unknown>, 'dataUpdatedAt' | 'isFetching' | 'isError' | 'error' | 'data'>

// When this data was last refreshed, and whether the last refresh failed. Says nothing about health.
export const LiveStatus = ({ query, className }: { query: LiveQuery; className?: string }) => {
  const updated = query.dataUpdatedAt ? formatTime(query.dataUpdatedAt) : null
  if (query.isError && query.data !== undefined)
    return (
      <Badge tone="warn" dot className={className} title={errorMessage(query.error)}>
        Stale · last update {updated}
      </Badge>
    )
  if (!updated) return null
  return (
    <span className={cn('inline-flex items-center gap-1.5 text-xs text-fg-subtle tabular', className)} aria-live="off">
      <span className={cn('size-1.5 rounded-full bg-accent', query.isFetching ? 'animate-pulse' : 'opacity-70')} aria-hidden />
      {query.isFetching ? 'Updating…' : `Updated ${updated}`}
    </span>
  )
}

// A backend severity with its issue counts. Missing severity reads "Unknown", never healthy.
export const HealthBadge = ({
  severity,
  errors,
  warnings,
  className,
}: {
  severity: Severity | null | undefined
  errors?: number | null
  warnings?: number | null
  className?: string
}) => {
  const counts = issueCountText(errors ?? null, warnings ?? null)
  return <SeverityBadge severity={severity === 'unavailable' ? 'unknown' : severity} label={counts || severityText(severity)} className={className} />
}

const ISSUE_ICON: Partial<Record<Severity, React.ComponentType<React.SVGProps<SVGSVGElement>>>> = {
  error: CircleExclamationIcon,
  warning: TriangleExclamationIcon,
  info: CircleInfoIcon,
}

export const SeverityIcon = ({ severity, className }: { severity: Severity; className?: string }) => {
  const Icon = ISSUE_ICON[severity] ?? (severity === 'healthy' ? CircleCheckIcon : CircleInfoIcon)
  return <Icon aria-hidden className={cn('size-4 shrink-0', toneClasses[severityTone(severity)].text, className)} />
}

// Backend warnings/errors, worst first, each linking to where it can be looked at.
export const IssueList = ({ issues, limit, className }: { issues: Issue[]; limit?: number; className?: string }) => {
  const sorted = sortIssues(issues)
  const shown = limit ? sorted.slice(0, limit) : sorted
  return (
    <ul className={cn('divide-y divide-line', className)}>
      {shown.map((issue, index) => (
        <li key={`${issue.code}-${issue.cardId ?? ''}-${index}`}>
          <Link
            href={healthHref(issue.href)}
            className="group flex items-start gap-3 px-1 py-2.5 transition-colors hover:bg-surface-1 focus-visible:bg-surface-1">
            <SeverityIcon severity={issue.severity} className="mt-0.5" />
            <span className="min-w-0 flex-1">
              {issue.title ? <span className="mr-2 text-sm font-medium text-fg">{issue.title}</span> : null}
              <span className="text-sm text-fg-muted">{issue.message}</span>
            </span>
            <ChevronRightIcon aria-hidden className="mt-1 size-3 shrink-0 text-fg-faint group-hover:text-fg-subtle" />
          </Link>
        </li>
      ))}
      {limit && sorted.length > limit ? (
        <li className="px-1 py-2 text-xs text-fg-subtle">and {sorted.length - limit} more</li>
      ) : null}
    </ul>
  )
}

// One overview metric as a tile. Unknown values render "not available"; only warning/error get color.
export const MetricTile = ({ metric, className }: { metric: Metric; className?: string }) => (
  <StatTile label={metric.label} value={metricDisplay(metric)} tone={metricTone(metric)} className={className} />
)

// A yes/no fact the backend reported (a service running, a dependency ready). null is "not available".
export const Fact = ({
  value,
  yes = 'Ready',
  no = 'Not ready',
}: {
  value: boolean | null | undefined
  yes?: string
  no?: string
}) => {
  const tone: Tone = value === true ? 'ok' : value === false ? 'danger' : 'unknown'
  return (
    <span className="inline-flex items-center gap-1.5">
      <span className={cn('size-1.5 shrink-0 rounded-full', toneClasses[tone].dot)} aria-hidden />
      <span className={value === undefined || value === null ? 'text-fg-faint' : 'text-fg'}>
        {value === true ? yes : value === false ? no : 'not available'}
      </span>
    </span>
  )
}

// A detail-page section: anchor id, the backend card's severity and issues in the header, live status, and the
// raw detail payload rendered through `children`.
export function HealthSection<T>({
  id,
  title,
  description,
  card,
  cardPending,
  query,
  actions,
  children,
  pending,
}: {
  id: string
  title: string
  description?: string
  card: Card | undefined
  cardPending?: boolean
  query: UseQueryResult<T>
  actions?: React.ReactNode
  children: (data: T) => React.ReactNode
  pending?: React.ReactNode
}) {
  const issues = card ? [...card.errors, ...card.warnings] : []
  return (
    <Panel
      id={id}
      className="scroll-mt-24"
      title={
        <span className="inline-flex flex-wrap items-center gap-2">
          {title}
          {card ? (
            <HealthBadge severity={card.severity} errors={card.errors.length} warnings={card.warnings.length} />
          ) : cardPending ? (
            <Skeleton className="h-6 w-20 rounded-full" />
          ) : (
            <HealthBadge severity="unknown" />
          )}
        </span>
      }
      description={card && !card.available ? (card.unavailableReason ?? description) : (card?.summary || description)}
      actions={
        <>
          {actions}
          <LiveStatus query={query} />
        </>
      }>
      {issues.length ? <IssueList issues={issues} className="mb-4 rounded-control border border-line px-2" /> : null}
      {query.isPending ? (
        (pending ?? <SectionSkeleton />)
      ) : query.data === undefined ? (
        <ErrorState error={query.error} onRetry={() => void query.refetch()} className="py-8" />
      ) : (
        children(query.data)
      )}
    </Panel>
  )
}

export const SectionSkeleton = () => (
  <div className="space-y-3" aria-hidden>
    <div className="grid grid-cols-2 gap-2.5 sm:grid-cols-4">
      {Array.from({ length: 4 }, (_, i) => (
        <Skeleton key={i} className="h-16 rounded-card" />
      ))}
    </div>
    <Skeleton className="h-24 rounded-card" />
  </div>
)

export const SubHeading = ({ children, className }: { children: React.ReactNode; className?: string }) => (
  <h3 className={cn('mb-2 text-xs font-medium tracking-wide text-fg-subtle uppercase', className)}>{children}</h3>
)
