'use client'

import React from 'react'
import Link from 'next/link'
import { Badge } from '@/components/ui/Badge'
import { EmptyState } from '@/components/ui/State'
import { Tooltip } from '@/components/ui/Tooltip'
import { ArrowLeftIcon, CircleInfoIcon, LockIcon } from '@/components/ui/icons'
import { titleCase } from '@/lib/format'
import { cn } from '@/util/cn'
import type { UserRecord } from '@/features/access/types'

export const roleLabel = (name: string | null | undefined) => (name ? titleCase(name) : '—')

export const userHref = (name: string) => `/users/${encodeURIComponent(name)}`
export const roleHref = (type: 'admin' | 'vault', id: number) => `/roles/${type}/${id}`

// A page the session can't use: the same copy the typed denied error renders.
export const DeniedState = ({ what }: { what?: string }) => (
  <EmptyState
    icon={LockIcon}
    title="You don't have access to this"
    description={`Your role doesn't include the permission ${what ? `to ${what}` : 'this page needs'}. Ask an administrator if you think it should.`}
  />
)

export const BackLink = ({ href, children }: { href: string; children: React.ReactNode }) => (
  <Link href={href} className="mb-3 inline-flex items-center gap-1.5 text-sm text-fg-subtle transition-colors hover:text-fg">
    <ArrowLeftIcon className="size-3.5" aria-hidden />
    {children}
  </Link>
)

export const AccountBadges = ({ user, selfId, className }: { user: UserRecord; selfId?: number; className?: string }) => (
  <span className={cn('inline-flex flex-wrap items-center gap-1.5', className)}>
    {user.id === selfId ? <Badge tone="accent">You</Badge> : null}
    {user.system_only ? (
      <Badge title="A service identity the daemon uses internally; it can't sign in to the console">System</Badge>
    ) : user.is_protected ? (
      <Badge title="Changed only from the server with the vh CLI">Protected</Badge>
    ) : null}
    {user.linux_uid !== undefined && !user.system_only ? (
      <Badge title={`Bound to Linux UID ${user.linux_uid} for the vh CLI`}>
        <span className="tabular">CLI · {user.linux_uid}</span>
      </Badge>
    ) : null}
  </span>
)

export const StatusBadge = ({ active }: { active: boolean }) =>
  active ? (
    <Badge tone="ok" dot>
      Active
    </Badge>
  ) : (
    <Badge tone="neutral" dot>
      Inactive
    </Badge>
  )

// A control the session can't use, with the reason one hover/focus away and repeated inline when asked.
export const Reason = ({ reason, className }: { reason?: string; className?: string }) =>
  reason ? (
    <p className={cn('flex items-start gap-1.5 text-xs text-fg-subtle', className)}>
      <CircleInfoIcon className="mt-px size-3.5 shrink-0" aria-hidden />
      <span>{reason}</span>
    </p>
  ) : null

export const WithReason = ({ reason, children }: { reason?: string; children: React.ReactElement }) =>
  reason ? (
    <Tooltip content={reason}>
      {/* A disabled button swallows pointer events; the wrapper keeps the tooltip reachable. */}
      <span tabIndex={0} className="inline-flex rounded-control focus-visible:outline-2 focus-visible:outline-accent">
        {children}
      </span>
    </Tooltip>
  ) : (
    children
  )

export const Notice = ({
  tone = 'info',
  title,
  children,
  className,
}: {
  tone?: 'info' | 'warn'
  title?: React.ReactNode
  children?: React.ReactNode
  className?: string
}) => (
  <div
    className={cn(
      'flex gap-3 rounded-card border px-4 py-3 text-sm',
      tone === 'warn' ? 'border-warn-line bg-warn-soft' : 'border-line bg-surface-1',
      className,
    )}>
    {tone === 'warn' ? (
      <LockIcon className="mt-0.5 size-4 shrink-0 text-warn" aria-hidden />
    ) : (
      <CircleInfoIcon className="mt-0.5 size-4 shrink-0 text-accent-text" aria-hidden />
    )}
    <div className="min-w-0 text-fg-muted">
      {title ? <p className="font-medium text-fg">{title}</p> : null}
      {children}
    </div>
  </div>
)
