'use client'

import React, { useMemo, useState } from 'react'
import { useWs } from '@/lib/query'
import { PageHeader } from '@/components/ui/Panel'
import { Select } from '@/components/ui/Field'
import { ShareLinkList } from '@/features/shares/ShareLinks'

export function SharesPage() {
  const vaults = useWs('storage.vault.list', null, { staleTime: 60_000 })
  const [vaultId, setVaultId] = useState<number | undefined>(undefined)
  const names = useMemo(() => new Map((vaults.data?.vaults ?? []).map(v => [v.id, v.name])), [vaults.data])

  return (
    <>
      <PageHeader
        title="Shares"
        description="Every share link across your vaults. Create new links from a file or folder in Files; full URLs are only shown when a link is created or rotated."
        actions={
          <div className="w-56">
            <Select aria-label="Filter by vault" value={vaultId ?? ''} onChange={event => setVaultId(event.target.value ? Number(event.target.value) : undefined)}>
              <option value="">All vaults</option>
              {(vaults.data?.vaults ?? []).map(v => (
                <option key={v.id} value={v.id}>
                  {v.name}
                </option>
              ))}
            </Select>
          </div>
        }
      />
      <ShareLinkList vaultId={vaultId} vaultNames={names} emptyText="No share links yet. Share a file or folder from Files." />
    </>
  )
}
