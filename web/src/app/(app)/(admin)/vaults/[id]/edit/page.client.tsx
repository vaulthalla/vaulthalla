'use client'

import { useVaultStore } from '@/stores/vaultStore'
import VaultForm from '@/components/vault/VaultForm'
import { useEffect, useState } from 'react'
import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'

const VaultEditPage = ({ id }: { id: number }) => {
  const [vault, setVault] = useState<LocalDiskVault | S3Vault | Vault | null>(null)

  useEffect(() => {
    const fetchVault = async () => {
      const vault = await useVaultStore.getState().getVault({ id })
      if (vault) setVault(vault)
    }

    fetchVault()
  }, [id])

  if (!vault) return <CircleNotchLoader />

  return <VaultForm initialValues={vault} />
}

export default VaultEditPage
