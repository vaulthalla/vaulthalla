'use client'

import React from 'react'
import { Badge } from '@/components/ui/Badge'
import { DASH, formatMoney } from '@/lib/format'
import type { Tone } from '@/lib/tone'
import type { BudgetPolicy } from '@/features/cost/model'

// Mode is configuration, not health: accent for the modes that act, neutral otherwise.
const MODE_TONE: Record<string, Tone> = { enforce: 'accent', warn: 'accent', report: 'neutral', off: 'neutral' }

export const ModeBadge = ({ policy }: { policy: BudgetPolicy | null }) =>
  !policy || !policy.is_active ?
    <Badge tone="neutral" className="text-fg-subtle">
      Not set
    </Badge>
  : <Badge tone={MODE_TONE[policy.mode] ?? 'neutral'}>
      {policy.mode.charAt(0).toUpperCase() + policy.mode.slice(1)}
    </Badge>

export const LimitsSummary = ({ policy }: { policy: BudgetPolicy | null }) => {
  if (!policy || !policy.is_active) return <span className="text-fg-faint">{DASH}</span>
  const parts = [
    policy.max_monthly_cost ? `${formatMoney(policy.max_monthly_cost, policy.currency)} / month` : null,
    policy.max_daily_cost ? `${formatMoney(policy.max_daily_cost, policy.currency)} / day` : null,
    policy.max_run_cost ? `${formatMoney(policy.max_run_cost, policy.currency)} / run` : null,
  ].filter(Boolean)
  return parts.length ?
      <span className="tabular text-fg-muted">{parts.join(' · ')}</span>
    : <span className="text-fg-subtle">No limits (records only)</span>
}

// A titled block whose body is its own surface (a DataTable, a panel grid): the heading sits outside it.
export const Section = ({
  title,
  description,
  actions,
  children,
  id,
}: {
  title: React.ReactNode
  description?: React.ReactNode
  actions?: React.ReactNode
  children: React.ReactNode
  id?: string
}) => (
  <section id={id} className="min-w-0 space-y-3" aria-labelledby={id ? `${id}-title` : undefined}>
    <div className="flex flex-wrap items-end justify-between gap-3">
      <div className="min-w-0">
        <h2 id={id ? `${id}-title` : undefined} className="text-fg text-[15px] font-semibold">
          {title}
        </h2>
        {description ?
          <p className="text-fg-subtle mt-0.5 max-w-3xl text-sm">{description}</p>
        : null}
      </div>
      {actions ?
        <div className="flex flex-wrap items-center gap-2">{actions}</div>
      : null}
    </div>
    {children}
  </section>
)

// An empty list without the header row of an empty table.
export const EmptyRows = ({ children }: { children: React.ReactNode }) => (
  <div className="panel text-fg-subtle px-6 py-8 text-center text-sm">{children}</div>
)

export const Money = ({
  value,
  currency,
  className,
}: {
  value: string | null | undefined
  currency?: string | null
  className?: string
}) =>
  value === null || value === undefined ?
    <span className="text-fg-faint">{DASH}</span>
  : <span className={className ?? 'tabular'}>{formatMoney(value, currency ?? 'USD')}</span>
