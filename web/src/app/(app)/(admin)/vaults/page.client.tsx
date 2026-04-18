'use client'

import { useVaultStore } from '@/stores/vaultStore'
import VaultCard from '@/components/vault/VaultCard'
import { useEffect } from 'react'

const VaultsClientPage = () => {
  const { vaults } = useVaultStore()

  useEffect(() => {
    useVaultStore.getState().fetchVaults()
  }, [])

  return vaults && vaults.length > 0 && vaults.map(vault => <VaultCard {...vault} key={vault.id} />)
}

export default VaultsClientPage
