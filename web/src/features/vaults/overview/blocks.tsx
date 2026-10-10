'use client'

import React from 'react'
import Link from 'next/link'
import { cn } from '@/util/cn'
import { SeverityBadge } from '@/components/ui/Badge'
import { Skeleton } from '@/components/ui/State'
import { Button } from '@/components/ui/Button'
import { ArrowsRotateIcon, ChevronRightIcon } from '@/components/ui/icons'
import { DASH, formatRelative, titleCase } from '@/lib/format'
import { toneClasses, type Tone } from '@/lib/tone'
import { errorMessage, isWsError } from '@/lib/ws/errors'

// Building blocks for the overview's stat panels: one panel per stats command, each with its own load state so a
// refused or failing command never blanks the whole page.

export const NA = 'not available'

const missing = (value: React.ReactNode) => value === null || value === undefined || value === DASH || value === ''

export const Metric = ({
  label,
  value,
  hint,
  tone,
  className,
}: {
  label: React.ReactNode
  value: React.ReactNode
  hint?: React.ReactNode
  tone?: Tone
  className?: string
}) => {
  const none = missing(value)
  return (
    <div className={cn('min-w-0', className)}>
      <dt className="truncate text-[11px] font-medium tracking-wide text-fg-subtle uppercase">{label}</dt>
      <dd className={cn('mt-0.5 truncate text-[15px] font-semibold text-fg tabular', none && 'text-sm font-normal text-fg-faint', !none && tone && toneClasses[tone].text)}>
        {none ? NA : value}
      </dd>
      {hint ? <dd className="truncate text-xs text-fg-subtle tabular">{hint}</dd> : null}
    </div>
  )
}

export const MetricGrid = ({ children, className }: { children: React.ReactNode; className?: string }) => (
  <dl className={cn('grid grid-cols-2 gap-x-5 gap-y-4 sm:grid-cols-3', className)}>{children}</dl>
)

interface QueryLike<T> {
  data: T | undefined
  error: unknown
  isPending: boolean
  refetch: () => unknown
  dataUpdatedAt?: number
}

// A backend status as a badge, labelled with the backend's own word.
export const StatusBadge = ({ status }: { status: string | null | undefined }) =>
  status ? <SeverityBadge severity={status} label={titleCase(status)} /> : <SeverityBadge severity={null} label="Unknown" />

export function StatPanel<T>({
  title,
  query,
  status,
  link,
  className,
  children,
}: {
  title: React.ReactNode
  query: QueryLike<T>
  status?: (data: T) => string | null | undefined
  link?: { href: string; label: string }
  className?: string
  children: (data: T) => React.ReactNode
}) {
  const data = query.data
  return (
    <section className={cn('panel flex min-w-0 flex-col', className)}>
      <header className="flex items-center justify-between gap-3 px-5 pt-4">
        <h2 className="truncate text-[15px] font-semibold text-fg">{title}</h2>
        {status && data !== undefined ? <StatusBadge status={status(data)} /> : null}
      </header>
      <div className="flex-1 px-5 pt-3.5 pb-5">
        {query.isPending ? (
          <div className="grid grid-cols-2 gap-4 sm:grid-cols-3" aria-busy>
            {Array.from({ length: 6 }, (_, i) => (
              <div key={i} className="space-y-1.5">
                <Skeleton className="h-3 w-16" />
                <Skeleton className="h-5 w-20" />
              </div>
            ))}
          </div>
        ) : query.error || data === undefined ? (
          <PanelError error={query.error} onRetry={() => void query.refetch()} />
        ) : (
          children(data)
        )}
      </div>
      {link || (data !== undefined && query.dataUpdatedAt) ? (
        <footer className="flex items-center justify-between gap-3 border-t border-line/70 px-5 py-2.5 text-xs text-fg-faint">
          <span className="tabular">{query.dataUpdatedAt && data !== undefined ? `Updated ${formatRelative(query.dataUpdatedAt)}` : ''}</span>
          {link ? (
            <Link href={link.href} className="inline-flex items-center gap-1 text-fg-subtle transition-colors hover:text-accent-text">
              {link.label}
              <ChevronRightIcon className="size-3" aria-hidden />
            </Link>
          ) : null}
        </footer>
      ) : null}
    </section>
  )
}

const PanelError = ({ error, onRetry }: { error: unknown; onRetry: () => void }) => {
  if (isWsError(error, 'denied'))
    return <p className="text-sm text-fg-subtle">Your role can’t read these statistics for this vault.</p>
  return (
    <div className="flex flex-wrap items-center gap-3">
      <p className="min-w-0 flex-1 text-sm text-fg-subtle">
        {isWsError(error, 'disconnected', 'timeout') ? 'Cannot reach the server.' : `Not available: ${errorMessage(error)}`}
      </p>
      <Button size="sm" variant="ghost" onClick={onRetry}>
        <ArrowsRotateIcon aria-hidden />
        Retry
      </Button>
    </div>
  )
}

// A short list (recent events, top users). Empty lists say so plainly.
export const MiniList = ({
  title,
  items,
  empty,
}: {
  title: string
  items: { key: string; primary: React.ReactNode; secondary?: React.ReactNode; meta?: React.ReactNode }[]
  empty: string
}) => (
  <div className="mt-5">
    <h3 className="mb-2 text-[11px] font-medium tracking-wide text-fg-subtle uppercase">{title}</h3>
    {items.length ? (
      <ul className="divide-y divide-line/60 rounded-control border border-line">
        {items.map(item => (
          <li key={item.key} className="flex items-center gap-3 px-3 py-2 text-sm">
            <div className="min-w-0 flex-1">
              <div className="truncate text-fg">{item.primary}</div>
              {item.secondary ? <div className="truncate text-xs text-fg-subtle">{item.secondary}</div> : null}
            </div>
            {item.meta ? <div className="shrink-0 text-xs text-fg-subtle tabular">{item.meta}</div> : null}
          </li>
        ))}
      </ul>
    ) : (
      <p className="text-sm text-fg-faint">{empty}</p>
    )}
  </div>
)

export const Note = ({ tone = 'neutral', children }: { tone?: Tone; children: React.ReactNode }) => (
  <p className={cn('mt-4 rounded-control border px-3 py-2 text-xs [&_svg]:text-inherit', toneClasses[tone].border, toneClasses[tone].bg, tone === 'neutral' ? 'text-fg-muted' : toneClasses[tone].text)}>
    {children}
  </p>
)

export const yesNo = (value: boolean | null | undefined) => (value === true ? 'Yes' : value === false ? 'No' : null)
