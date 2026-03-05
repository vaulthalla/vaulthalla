'use client'

import React, { useMemo, useState } from 'react'
import { useRouter } from 'next/navigation'
import { usePermsStore } from '@/stores/permissionStore'
import RoleForm, { RoleFormData, RoleType } from '@/components/roles/RoleForm'
import type { UserRole, VaultRole } from '@/models/role'

type RoleEditorProps = {
  type: RoleType
  mode: 'create' | 'edit'
  defaultValues?:
    | Partial<Pick<UserRole, 'name' | 'description' | 'permissions'>>
    | Partial<Pick<VaultRole, 'name' | 'description' | 'permissions'>>
  redirectTo?: string
  title?: string
}

export const RoleEditor = ({ type, mode, defaultValues, redirectTo = '/dashboard/roles', title }: RoleEditorProps) => {
  const router = useRouter()
  const store = usePermsStore.getState()
  const [error, setError] = useState<string | null>(null)
  const [saving, setSaving] = useState(false)

  const heading = useMemo(() => {
    if (title) return title
    return mode === 'create' ?
        `Add ${type === 'vault' ? 'Vault' : 'User'} Role`
      : `Edit ${type === 'vault' ? 'Vault' : 'User'} Role`
  }, [mode, type, title])

  const onSubmit = async (data: RoleFormData) => {
    setError(null)
    setSaving(true)

    try {
      if (mode === 'create') {
        await store.addRole(data)
      } else {
        // adjust signature to your store API (id, patch, etc.)
        await store.updateRole(data)
      }

      if (data.type === 'vault') await store.fetchVaultRoles()
      else await store.fetchUserRoles()

      router.push(redirectTo)
      router.refresh() // nice when server components also depend on this data
    } catch (e: any) {
      console.error(e)
      setError(e?.message ?? 'Failed to save role.')
    } finally {
      setSaving(false)
    }
  }

  return (
    <div className="mx-auto max-w-3xl p-4">
      <div className="mb-4">
        <h1 className="text-2xl font-bold text-cyan-100">{heading}</h1>
        <p className="mt-1 text-sm text-cyan-300/80">
          {type === 'vault' ? 'Vault-scoped permissions.' : 'Admin and platform permissions.'}
        </p>
      </div>

      {error && (
        <div className="mb-4 rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-sm text-red-200">{error}</div>
      )}

      {/* RoleForm already injects the hidden `type` field from its prop */}
      <RoleForm type={type} defaultValues={defaultValues} action={onSubmit} />

      {saving && <div className="mt-3 text-right text-xs text-cyan-300/70">Saving…</div>}
    </div>
  )
}
