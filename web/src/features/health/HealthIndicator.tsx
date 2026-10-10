'use client'

import React from 'react'
import Link from 'next/link'
import { cn } from '@/util/cn'
import { useWs } from '@/lib/query'
import { STATS_VIEW, useCan } from '@/lib/permissions'
import { severityTone, toneClasses } from '@/lib/tone'
import { isWsError } from '@/lib/ws/errors'
import { asSeverity, issueCountText, severityText } from '@/features/health/model'

const POLL_MS = 30_000

// Daemons without stats.dashboard.severity answer "Unknown command"; the indicator then stays out of the way.
const unsupported = (error: unknown) => isWsError(error) && /unknown command/i.test(error.message)

// Top-bar health dot for accounts with admin.stats.view. Reads the cheap severity rollup only (never the full
// overview) and links to /health. Renders nothing without the permission, on daemons without the command, and before
// the first answer.
export const HealthIndicator = ({ className }: { className?: string }) => {
  const canView = useCan(STATS_VIEW)
  const query = useWs('stats.dashboard.severity', null, {
    enabled: canView,
    staleTime: POLL_MS - 5_000,
    retry: false,
    refetchInterval: q => (unsupported(q.state.error) || isWsError(q.state.error, 'denied') ? false : POLL_MS),
  })

  if (!canView || query.isPending) return null
  if (query.error && (unsupported(query.error) || isWsError(query.error, 'denied'))) return null

  // A failed refresh keeps the last answer only while it is still fresh enough to trust; otherwise it's unknown.
  const raw = query.data as { stats?: Record<string, unknown> } & Record<string, unknown> | undefined
  const data = (raw?.stats ?? raw ?? {}) as Record<string, unknown>
  const stale = query.isError
  const severity = stale ? 'unknown' : asSeverity(data.overall_status)
  const errors = typeof data.error_count === 'number' ? data.error_count : null
  const warnings = typeof data.warning_count === 'number' ? data.warning_count : null
  const label = stale ? 'Health unknown' : issueCountText(errors, warnings) || severityText(severity)
  const tone = toneClasses[severityTone(severity)]

  return (
    <Link
      href="/health"
      title={stale ? 'The last health check failed' : `System health: ${label}`}
      aria-label={`System health: ${label}`}
      className={cn(
        'inline-flex h-8 items-center gap-2 rounded-full border border-line bg-surface-1 px-2.5 text-xs text-fg-muted transition-colors hover:border-line-strong hover:text-fg',
        className,
      )}>
      <span className="relative flex size-2 shrink-0" aria-hidden>
        {severity === 'error' ? <span className={cn('absolute inline-flex size-full animate-ping rounded-full opacity-60', tone.dot)} /> : null}
        <span className={cn('relative inline-flex size-2 rounded-full', tone.dot)} />
      </span>
      <span className={cn('hidden whitespace-nowrap sm:inline', (errors || warnings) && tone.text)}>{label}</span>
    </Link>
  )
}
