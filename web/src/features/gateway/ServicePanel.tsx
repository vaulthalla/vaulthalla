'use client'

import React from 'react'
import Link from 'next/link'
import { Panel } from '@/components/ui/Panel'
import { Badge } from '@/components/ui/Badge'
import { StatGrid, StatTile } from '@/components/ui/Stat'
import { ErrorState, Skeleton } from '@/components/ui/State'
import { formatInt } from '@/lib/format'
import { bindAddress, isWildcardBind, type GatewayStatus } from '@/features/gateway/model'

// Running/stopped are direct facts from the daemon; an unknown status stays unknown (#144).
const StateBadge = ({ status }: { status: GatewayStatus | null }) => {
  if (!status || status.running === null) return <Badge tone="unknown">Status unknown</Badge>
  if (!status.running)
    return (
      <Badge tone="neutral" dot>
        Stopped
      </Badge>
    )
  if (status.ready === false)
    return (
      <Badge tone="info" dot>
        Starting
      </Badge>
    )
  return (
    <Badge tone="ok" dot>
      Running
    </Badge>
  )
}

export const ServicePanel = ({
  query,
}: {
  query: { data: GatewayStatus | null | undefined; isPending: boolean; error: unknown; refetch: () => unknown }
}) => {
  if (query.isPending)
    return (
      <Panel title="Service">
        <StatGrid className="xl:grid-cols-4">
          {Array.from({ length: 4 }, (_, i) => (
            <Skeleton key={i} className="rounded-card h-[74px]" />
          ))}
        </StatGrid>
      </Panel>
    )
  if (query.error)
    return (
      <Panel title="Service">
        <ErrorState error={query.error} onRetry={() => void query.refetch()} className="py-8" />
      </Panel>
    )

  const s = query.data ?? null
  const bind = bindAddress(s)
  return (
    <Panel
      title={
        <span className="flex items-center gap-3">
          Service <StateBadge status={s} />
        </span>
      }
      description={
        s?.running === false ?
          <>
            The gateway is off. Turn it on with{' '}
            <code className="text-fg-muted font-mono text-xs">s3_gateway.enabled</code> in{' '}
            <Link href="/settings#s3_gateway" className="text-accent-text hover:underline">
              Settings
            </Link>
            .
          </>
        : undefined
      }>
      <StatGrid className="xl:grid-cols-4">
        <StatTile
          label="Bind address"
          value={bind ? <span className="font-mono text-base">{bind}</span> : null}
          hint={
            isWildcardBind(s) ? 'Listens on every interface'
            : s?.configured === false ?
              'Not configured'
            : undefined
          }
        />
        <StatTile
          label="Active sessions"
          value={s?.active_sessions === null || !s ? null : formatInt(s.active_sessions)}
        />
        <StatTile
          label="Requests"
          value={s?.total_requests === null || !s ? null : formatInt(s.total_requests)}
          hint="Since the gateway started"
        />
        <StatTile
          label="Failed requests"
          value={s?.failed_requests === null || !s ? null : formatInt(s.failed_requests)}
        />
      </StatGrid>
    </Panel>
  )
}
