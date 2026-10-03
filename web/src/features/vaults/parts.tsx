'use client'

import React from 'react'
import { cn } from '@/util/cn'
import { Meter } from '@/components/ui/Stat'
import { CloudIcon, HardDriveIcon } from '@/components/ui/icons'
import { formatBytes, formatPercent } from '@/lib/format'

// One row of stats.system.storage / stats.vault.storage `vaults`.
export interface BackendEntry {
  vault_id: number
  vault_name?: string
  type?: string
  active?: boolean
  backend_status?: string | null
  bucket?: string | null
  vault_size_bytes?: number | null
  quota_bytes?: number | null
  free_space_bytes?: number | null
  cache_size_bytes?: number | null
  min_free_space_ok?: boolean | null
  upstream_encryption_enabled?: boolean | null
  provider_latency_avg_ms?: number | null
  provider_latency_max_ms?: number | null
  provider_errors_total?: number | null
  provider_ops_total?: number | null
  provider_error_rate?: number | null
  last_provider_error?: string | null
  last_provider_success_at?: number | string | null
}

export const VaultGlyph = ({ type, className }: { type: string; className?: string }) => {
  const Icon = type === 's3' ? CloudIcon : HardDriveIcon
  return (
    <span
      className={cn(
        'grid size-8 shrink-0 place-items-center rounded-control border border-line bg-surface-2 text-accent-text [&_svg]:size-4',
        className,
      )}
      aria-hidden>
      <Icon />
    </span>
  )
}

// Used bytes against the quota. Quota 0 means unlimited: the bar is omitted rather than drawn against nothing.
export const UsageCell = ({ used, quota, className }: { used: number | null | undefined; quota: number | null | undefined; className?: string }) => {
  const known = typeof used === 'number' && Number.isFinite(used)
  const limited = typeof quota === 'number' && quota > 0
  return (
    <div className={cn('w-40 min-w-0', className)}>
      <div className="flex items-baseline justify-between gap-2 text-xs tabular">
        <span className={known ? 'text-fg-muted' : 'text-fg-faint'}>{known ? formatBytes(used) : 'not available'}</span>
        <span className="text-fg-subtle">{limited ? `of ${formatBytes(quota)}` : 'no quota'}</span>
      </div>
      {limited ? <Meter className="mt-1.5" ratio={known ? (used as number) / (quota as number) : null} label="Quota used" /> : null}
    </div>
  )
}

export const quotaRatioText = (used: number | null | undefined, quota: number | null | undefined) =>
  typeof used === 'number' && typeof quota === 'number' && quota > 0 ? formatPercent(used / quota) : null
