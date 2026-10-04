'use client'

import React from 'react'
import Link from 'next/link'
import { useWs } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { DataTable } from '@/components/ui/DataTable'
import { EmptyState, QueryState } from '@/components/ui/State'
import { ArrowUpRightFromSquareIcon, BucketIcon } from '@/components/ui/icons'
import { formatDate, titleCase } from '@/lib/format'
import { useCurrentVault } from '@/features/vaults/VaultShell'

interface Binding {
  bucket_name: string
  bucket?: string
  vault_id: number
  mode?: string
  api_exclusive?: boolean
  created_at?: number | string | null
}

// Read-only: which S3 gateway buckets expose this vault. Binding and credentials are managed on /s3-gateway.
export const VaultGateway = () => {
  const vault = useCurrentVault()
  const canView = useCan({ permission: 'admin.s3_gateway.view' })
  const buckets = useWs('s3.gateway.buckets.list', null, {
    enabled: canView,
    retry: false,
    select: d => ((d.buckets ?? []) as unknown as Binding[]).filter(b => Number(b.vault_id) === vault.id),
  })
  const status = useWs('s3.gateway.status', null, { enabled: canView, retry: false })
  const running = status.data?.status?.running

  const manage = (
    <Button asChild variant="subtle">
      <Link href="/s3-gateway">
        <ArrowUpRightFromSquareIcon aria-hidden />
        Manage the S3 gateway
      </Link>
    </Button>
  )

  if (!canView)
    return (
      <div className="panel">
        <EmptyState title="You don’t have access to the S3 gateway" description="Your role doesn’t include admin.s3_gateway.view." />
      </div>
    )

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <p className="max-w-2xl text-sm text-fg-subtle">
          The S3 gateway serves vaults to S3 clients (aws-cli, rclone, backup tools) as buckets.
          {running === false ? ' The gateway is not running right now.' : ''}
        </p>
        {manage}
      </div>
      <QueryState query={buckets}>
        {rows =>
          rows.length ? (
            <DataTable
              rows={rows}
              rowKey={b => b.bucket_name}
              columns={[
                { key: 'bucket', header: 'Bucket', cell: b => <span className="font-mono text-fg">{b.bucket_name || b.bucket}</span> },
                { key: 'mode', header: 'Mode', cell: b => <Badge tone="neutral">{titleCase(b.mode ?? 'local')}</Badge> },
                {
                  key: 'exclusive',
                  header: 'Access',
                  hideBelow: 'sm',
                  cell: b => <span className="text-fg-muted">{b.api_exclusive ? 'Gateway only' : 'Shared with files'}</span>,
                },
                { key: 'created', header: 'Bound', hideBelow: 'md', cell: b => <span className="text-fg-subtle tabular">{formatDate(b.created_at)}</span> },
              ]}
            />
          ) : (
            <div className="panel">
              <EmptyState
                icon={BucketIcon}
                title="Not exposed through the S3 gateway"
                description="No gateway bucket is bound to this vault. Bind one on the S3 gateway page to reach it with S3 clients."
              />
            </div>
          )
        }
      </QueryState>
    </div>
  )
}
