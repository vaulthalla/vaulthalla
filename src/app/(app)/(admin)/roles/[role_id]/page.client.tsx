'use client'

import RoleForm, { RoleFormData } from '@/components/roles/RoleForm'
import { AdminRole, VaultRole } from '@/models/role'
import { useEffect, useState } from 'react'
import { usePermsStore } from '@/stores/permissionStore'
import { useRouter } from 'next/navigation'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'

const EditRoleClientPage = ({ id }: { id: number }) => {
  const [role, setRole] = useState<AdminRole | VaultRole | null>(null)
  const router = useRouter()

  useEffect(() => {
    const fetchRole = async () => {
      const roleData = await usePermsStore.getState().getRole({ id })
      if (roleData) setRole(roleData)
      else console.error(`Role with ID ${id} not found`)
    }

    fetchRole().catch(err => console.error('Error fetching role:', err))
  }, [id])

  const onSubmit = async (data: RoleFormData) => {
    try {
      await usePermsStore.getState().updateRole({ ...data })
      console.log('Role updated successfully')
      if (role?.type === 'user') await usePermsStore.getState().fetchUserRoles()
      else if (role?.type === 'vault') await usePermsStore.getState().fetchVaultRoles()
      router.push('/dashboard/roles')
    } catch (error) {
      console.error('Error updating role:', error)
    }
  }

  if (!role) return <CircleNotchLoader />

  return (
    <div>
      <h1>Edit Role {id}</h1>
      <RoleForm action={onSubmit} defaultValues={role} />
    </div>
  )
}

export default EditRoleClientPage
