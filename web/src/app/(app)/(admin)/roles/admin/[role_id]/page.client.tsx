'use client'

import { useEffect, useState } from 'react'
import { useRouter } from 'next/navigation'
import RoleForm from '@/components/roles/RoleForm'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { AdminRole } from '@/models/role'
import { useAdminRoleStore } from '@/stores/useAdminRoleStore'

export default function EditAdminRoleClientPage({ id }: { id: number }) {
  const router = useRouter()
  const [role, setRole] = useState<AdminRole | null>(null)
  const [isLoading, setIsLoading] = useState(true)

  useEffect(() => {
    let mounted = true

    const fetchRole = async () => {
      try {
        const roleData = await useAdminRoleStore.getState().getAdminRole({ id })

        if (!mounted) return

        if (roleData) setRole(roleData)
        else console.error(`Role with ID ${id} not found`)
      } catch (err) {
        console.error('Error fetching role:', err)
      } finally {
        if (mounted) setIsLoading(false)
      }
    }

    void fetchRole()

    return () => {
      mounted = false
    }
  }, [id])

  if (isLoading) return <CircleNotchLoader />
  if (!role) return <div className="text-white/70">Role not found.</div>

  return (
    <div className="space-y-6">
      <h1 className="text-2xl font-semibold text-white">Edit Admin role</h1>

      <RoleForm type="admin" defaultValues={role} onSavedAction={async () => router.push('/roles/admin')} />
    </div>
  )
}
