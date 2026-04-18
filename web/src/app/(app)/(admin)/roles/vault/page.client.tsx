'use client'

import RolesComponent from '@/components/roles/RolesComponent'
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'

const VaultRolesClientPage = () => {
  const { vaultRoles } = useVaultRoleStore()
  return <RolesComponent roles={vaultRoles} />
}

export default VaultRolesClientPage
