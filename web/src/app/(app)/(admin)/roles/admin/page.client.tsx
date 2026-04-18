'use client'

import RolesComponent from '@/components/roles/RolesComponent'
import { useAdminRoleStore } from '@/stores/useAdminRoleStore'

const UserRolesClientPage = () => {
  const { adminRoles } = useAdminRoleStore()
  return <RolesComponent roles={adminRoles} />
}

export default UserRolesClientPage
