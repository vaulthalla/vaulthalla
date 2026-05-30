'use client'

import { useVaultStore } from '@/stores/vaultStore'
import { useEffect, useState } from 'react'
import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import VaultStatsDashboard from '@/components/vault/VaultStatsDashboard/Component'
import VaultHero from '@/components/vault/VaultHero'
import AssignmentsCard from '@/components/vault/AssignmentsCard'
import Link from 'next/link'
import { Button } from '@/components/Button'

const VaultPage = ({ id }: { id: number }) => {
  const [vault, setVault] = useState<LocalDiskVault | S3Vault | Vault | null>(null)

  useEffect(() => {
    const fetchVault = async () => {
      const vault = await useVaultStore.getState().getVault({ id })
      if (vault) setVault(vault)
    }

    fetchVault()
  }, [id])

  if (!vault) return <CircleNotchLoader />

  const editHref = `/vaults/${id}/edit`
  const guardrailsHref = `${editHref}?section=s3-guardrails`
  const actions = (
    <div className="flex w-full flex-col gap-2 sm:w-auto sm:flex-row">
      {vault.type === 's3' && (
        <Button asChild variant="glow" size="sm" className="my-0 w-full text-sm sm:w-auto">
          <Link href={guardrailsHref}>Cost Controls</Link>
        </Button>
      )}
      <Button asChild variant="outline" size="sm" className="my-0 w-full text-sm sm:w-auto">
        <Link href={editHref}>Edit Vault</Link>
      </Button>
    </div>
  )

  return (
    <div className="space-y-4 text-center">
      <VaultHero vault={vault} rightSlot={actions} />
      <div className="grid gap-4">
        <AssignmentsCard vault={vault} />
      </div>
      <VaultStatsDashboard vault_id={id} />
    </div>
  )
}

export default VaultPage
