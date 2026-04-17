'use client'

import { useEffect, useState } from 'react'
import { useVaultStore } from '@/stores/vaultStore'
import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { AssignUserForm } from '@/components/roles/vault/AssignUserForm'

const AssignUserClientPage = ({ id }: { id: number }) => {
  const [vault, setVault] = useState<LocalDiskVault | S3Vault | Vault | null>(null)

  useEffect(() => {
    let mounted = true

    const fetchVault = async () => {
      try {
        const fetchedVault = await useVaultStore.getState().getVault({ id })
        if (mounted) setVault(fetchedVault)
      } catch (err) {
        console.error('Failed to fetch vault for assignment:', err)
      }
    }

    void fetchVault()
    return () => {
      mounted = false
    }
  }, [id])

  if (!vault) return <CircleNotchLoader />

  return <AssignUserForm vault={vault} />
}

export default AssignUserClientPage
