'use client'

import { useEffect } from 'react'
import RolesComponent from '@/components/roles/RolesComponent'
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'

const VaultRolesClientPage = () => {
  const { vaultRoles, fetchVaultRoles } = useVaultRoleStore()

  useEffect(() => {
    void fetchVaultRoles()
  }, [fetchVaultRoles])

  return <RolesComponent roles={vaultRoles} />
}

export default VaultRolesClientPage
