'use client'

import { useMemo } from 'react'
import { useRouter } from 'next/navigation'
import RoleForm, { RoleType } from '@/components/roles/RoleForm'
import type { AdminRole, VaultRole } from '@/models/role'

type RoleEditorProps = {
  type: RoleType
  mode: 'create' | 'edit'
  defaultValues?:
    | Partial<Pick<AdminRole, 'name' | 'description' | 'permissions'>>
    | Partial<Pick<VaultRole, 'name' | 'description' | 'permissions'>>
  redirectTo?: string
  title?: string
}

export const RoleEditor = ({ type, mode, defaultValues, redirectTo = '/dashboard/roles', title }: RoleEditorProps) => {
  const router = useRouter()

  const heading = useMemo(() => {
    if (title) return title
    return mode === 'create' ?
        `Add ${type === 'vault' ? 'Vault' : 'User'} Role`
      : `Edit ${type === 'vault' ? 'Vault' : 'User'} Role`
  }, [mode, type, title])

  return (
    <div className="mx-auto max-w-3xl p-4">
      <div className="mb-4">
        <h1 className="text-2xl font-bold text-cyan-100">{heading}</h1>
        <p className="mt-1 text-sm text-cyan-300/80">
          {type === 'vault' ? 'Vault-scoped permissions.' : 'Admin and platform permissions.'}
        </p>
      </div>

      <RoleForm
        type={type}
        defaultValues={defaultValues}
        onSavedAction={() => {
          router.push(redirectTo)
          router.refresh()
        }}
      />
    </div>
  )
}
