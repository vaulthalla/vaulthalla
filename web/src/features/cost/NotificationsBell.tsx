'use client'

import React, { useState } from 'react'
import Link from 'next/link'
import * as Popover from '@radix-ui/react-popover'
import { cn } from '@/util/cn'
import { useIsAdmin } from '@/lib/permissions'
import { formatRelative } from '@/lib/format'
import { severityTone, toneClasses } from '@/lib/tone'
import { Button } from '@/components/ui/Button'
import { Tooltip } from '@/components/ui/Tooltip'
import { BellIcon, CheckIcon } from '@/components/ui/icons'
import { useNotifications } from '@/features/cost/queries'
import { ackNotification } from '@/features/cost/alerts'
import type { PriceNotification } from '@/features/cost/model'

const RANK: Record<string, number> = { critical: 4, error: 3, warning: 2, info: 1 }

// The worst open alert sets the badge tone; the severity comes from the backend.
const worst = (items: PriceNotification[]) =>
  items.reduce<string | null>(
    (acc, n) => ((RANK[n.severity ?? ''] ?? 0) > (RANK[acc ?? ''] ?? 0) ? n.severity : acc),
    null,
  )

// Top-bar bell for open budget/pricing alerts. Admins only; renders nothing for everyone else.
export const NotificationsBell = () => {
  const isAdmin = useIsAdmin()
  const [open, setOpen] = useState(false)
  const alerts = useNotifications(isAdmin, false, 60_000)
  if (!isAdmin) return null

  const items = alerts.data ?? []
  const count = items.length
  const tone = severityTone(worst(items))
  const label =
    alerts.error ? 'Cost alerts unavailable'
    : count ? `${count} open cost alert${count === 1 ? '' : 's'}`
    : 'Cost alerts'

  return (
    <Popover.Root open={open} onOpenChange={setOpen}>
      <Tooltip content={label}>
        <Popover.Trigger asChild>
          <Button variant="ghost" size="icon" aria-label={label} className="relative">
            <BellIcon aria-hidden />
            {count > 0 ?
              <span
                className={cn(
                  'tabular text-bg absolute top-1 right-1 grid h-4 min-w-4 place-items-center rounded-full px-1 text-[10px] leading-none font-semibold',
                  toneClasses[tone].dot,
                )}>
                {count > 9 ? '9+' : count}
              </span>
            : null}
          </Button>
        </Popover.Trigger>
      </Tooltip>
      <Popover.Portal>
        <Popover.Content
          align="end"
          sideOffset={8}
          collisionPadding={8}
          className="glass-strong animate-pop-in rounded-card z-[65] w-[min(24rem,calc(100vw-1rem))] focus:outline-none">
          <div className="border-line flex items-center justify-between border-b px-4 py-3">
            <span className="text-fg text-sm font-semibold">Cost alerts</span>
            <Link href="/cost" onClick={() => setOpen(false)} className="text-accent-text text-xs hover:underline">
              Open cost control
            </Link>
          </div>
          <div className="max-h-[min(60vh,28rem)] overflow-y-auto p-1.5">
            {alerts.isPending ?
              <p className="text-fg-subtle px-3 py-6 text-center text-sm">Loading…</p>
            : alerts.error ?
              <p className="text-fg-subtle px-3 py-6 text-center text-sm">Alerts can’t be loaded right now.</p>
            : items.length === 0 ?
              <p className="text-fg-subtle px-3 py-6 text-center text-sm">No open alerts.</p>
            : <ul>
                {items.map(n => (
                  <li key={n.id} className="group hover:bg-surface-2 flex gap-3 rounded-md px-2.5 py-2">
                    <span
                      className={cn('mt-1.5 size-2 shrink-0 rounded-full', toneClasses[severityTone(n.severity)].dot)}
                      aria-hidden
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex items-baseline justify-between gap-2">
                        <span className="text-fg truncate text-sm font-medium">{n.title ?? 'Budget alert'}</span>
                        <span className="tabular text-fg-faint shrink-0 text-[11px]">
                          {formatRelative(n.created_at)}
                        </span>
                      </div>
                      {n.message ?
                        <p className="text-fg-subtle line-clamp-2 text-xs">{n.message}</p>
                      : null}
                    </div>
                    <button
                      type="button"
                      onClick={() => void ackNotification(n)}
                      aria-label={`Acknowledge ${n.title ?? 'alert'}`}
                      className="text-fg-subtle hover:bg-surface-3 hover:text-fg self-center rounded-md p-1.5 [&_svg]:size-3.5">
                      <CheckIcon aria-hidden />
                    </button>
                  </li>
                ))}
              </ul>
            }
          </div>
        </Popover.Content>
      </Popover.Portal>
    </Popover.Root>
  )
}
