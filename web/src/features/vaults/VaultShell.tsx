'use client'

import React, { createContext, useContext } from 'react'
import Link from 'next/link'
import { useWs, invalidate } from '@/lib/query'
import { api } from '@/lib/session'
import { Button } from '@/components/ui/Button'
import { Badge, SeverityBadge } from '@/components/ui/Badge'
import { LinkTabs } from '@/components/ui/Tabs'
import { ErrorState, Skeleton } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import {
  ArrowsRotateIcon,
  ChartLineIcon,
  ChevronLeftIcon,
  CloudIcon,
  FolderOpenIcon,
  GearIcon,
  ShareNodesIcon,
  UsersIcon,
  SackDollarIcon,
} from '@/components/ui/icons'
import { formatDate, titleCase } from '@/lib/format'
import { useCredentials, useUserNames, useVault, useVaultId } from '@/features/vaults/hooks'
import { providerLabel, type VaultDetail } from '@/features/vaults/model'
import { UsageCell, VaultGlyph, type BackendEntry } from '@/features/vaults/parts'

const VaultContext = createContext<VaultDetail | null>(null)

// The vault every tab under /vaults/[id] renders. The shell only renders tabs once it is loaded.
export const useCurrentVault = () => {
  const vault = useContext(VaultContext)
  if (!vault) throw new Error('useCurrentVault outside VaultShell')
  return vault
}

export const STATS_POLL_MS = 15_000

// Backend storage row for one vault (size, quota, backend status); shared by the header and the overview.
export const useVaultBackend = (vaultId: number) =>
  useWs('stats.vault.storage', { vault_id: vaultId }, {
    refetchInterval: STATS_POLL_MS,
    retry: false,
    select: data =>
      ((data.stats as unknown as { vaults?: BackendEntry[] })?.vaults ?? []).find(v => v.vault_id === vaultId) ?? null,
  })

export const VaultShell = ({ children }: { children: React.ReactNode }) => {
  const id = useVaultId()
  if (!id) return <ErrorState error={new Error('That is not a vault id.')} />
  return <VaultShellLoaded id={id}>{children}</VaultShellLoaded>
}

const VaultShellLoaded = ({ id, children }: { id: number; children: React.ReactNode }) => {
  const vault = useVault(id)

  return (
    <div className="min-w-0">
      <Link href="/vaults" className="mb-3 inline-flex items-center gap-1.5 text-xs text-fg-subtle transition-colors hover:text-fg">
        <ChevronLeftIcon className="size-3" aria-hidden />
        Vaults
      </Link>
      {vault.isPending ? (
        <HeaderSkeleton />
      ) : vault.error || !vault.data ? (
        <div className="panel">
          <ErrorState error={vault.error} onRetry={() => void vault.refetch()} />
        </div>
      ) : (
        <VaultContext.Provider value={vault.data}>
          <VaultHeader vault={vault.data} />
          <LinkTabs
            className="mt-5"
            tabs={[
              { href: `/vaults/${id}`, label: 'Overview', icon: ChartLineIcon, exact: true },
              { href: `/vaults/${id}/access`, label: 'Access', icon: UsersIcon },
              { href: `/vaults/${id}/shares`, label: 'Shares', icon: ShareNodesIcon },
              { href: `/vaults/${id}/sync`, label: 'Sync & cost', icon: SackDollarIcon },
              { href: `/vaults/${id}/gateway`, label: 'Gateway', icon: CloudIcon },
              { href: `/vaults/${id}/settings`, label: 'Settings', icon: GearIcon },
            ]}
          />
          <div className="pt-6">{children}</div>
        </VaultContext.Provider>
      )}
    </div>
  )
}

const HeaderSkeleton = () => (
  <div className="flex items-center gap-4 py-2" aria-busy>
    <Skeleton className="size-12 rounded-card" />
    <div className="flex-1 space-y-2">
      <Skeleton className="h-6 w-56" />
      <Skeleton className="h-4 w-80" />
    </div>
  </div>
)

const SyncNowButton = ({ vault }: { vault: VaultDetail }) => {
  const [busy, setBusy] = React.useState(false)
  const run = async () => {
    setBusy(true)
    try {
      const res = await api.send('storage.vault.sync', { id: vault.id })
      notify.success(res.status === 'rerun_queued' ? 'A sync is already running; another run is queued' : 'Sync started')
      void invalidate('stats.vault.sync', 'stats.vault')
    } catch (error) {
      notify.error(error, 'Could not start a sync')
    } finally {
      setBusy(false)
    }
  }
  return (
    <Button onClick={run} loading={busy}>
      {busy ? null : <ArrowsRotateIcon aria-hidden />}
      Sync now
    </Button>
  )
}

const VaultHeader = ({ vault }: { vault: VaultDetail }) => {
  const credentials = useCredentials(vault.type === 's3')
  const { names } = useUserNames()
  const backend = useVaultBackend(vault.id)
  const owner = vault.owner || names.get(vault.owner_id)
  const status = backend.data?.backend_status

  return (
    <header className="flex flex-wrap items-start gap-x-6 gap-y-4">
      <div className="flex min-w-0 flex-1 items-start gap-4">
        <VaultGlyph type={vault.type} className="mt-0.5 size-12 rounded-card [&_svg]:size-5" />
        <div className="min-w-0">
          <h1 className="truncate text-2xl font-semibold tracking-tight text-fg">{vault.name}</h1>
          {vault.description ? <p className="mt-0.5 max-w-2xl truncate text-sm text-fg-subtle">{vault.description}</p> : null}
          <div className="mt-2.5 flex flex-wrap items-center gap-2">
            {!vault.is_active ? (
              <Badge tone="neutral">Inactive</Badge>
            ) : status ? (
              <span title="Storage backend status reported by the server">
                <SeverityBadge severity={status} label={titleCase(status)} />
              </span>
            ) : (
              <Badge tone="neutral">Active</Badge>
            )}
            <Badge tone="accent">{providerLabel(vault, credentials.data)}</Badge>
            {vault.type === 's3' && vault.bucket ? (
              <Badge className="font-mono">{vault.bucket}</Badge>
            ) : null}
            <span className="text-xs text-fg-subtle">
              Owner{' '}
              {owner ? (
                <span className="text-fg-muted">{owner}</span>
              ) : vault.owner_id ? (
                <span className="tabular">#{vault.owner_id}</span>
              ) : (
                'none'
              )}
              <span className="mx-1.5 text-fg-faint">·</span>
              Created <span className="tabular">{formatDate(vault.created_at)}</span>
            </span>
          </div>
        </div>
      </div>
      <div className="flex flex-wrap items-center gap-4">
        <UsageCell used={backend.data?.vault_size_bytes} quota={vault.quota} className="w-48" />
        <div className="flex items-center gap-2">
          {vault.type === 's3' ? <SyncNowButton vault={vault} /> : null}
          <Button asChild variant="subtle">
            <Link href={`/files/${vault.id}`}>
              <FolderOpenIcon aria-hidden />
              Open files
            </Link>
          </Button>
        </div>
      </div>
    </header>
  )
}
