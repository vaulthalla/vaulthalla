'use client'

import { useEffect, useMemo, useRef, useState } from 'react'
import * as motion from 'motion/react-client'
import { Button } from '@/components/Button'
import { AdminRole, VaultRole, Permission, PermissionPayload } from '@/models/role'
import RolePermissionsForm from '@/components/roles/RolePermissionsForm'
import { usePermStore } from '@/stores/usePermStore'
import { useAdminRoleStore } from '@/stores/useAdminRoleStore'
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'
import { SubjectType } from '@/models/permission'

export type RoleType = 'admin' | 'vault'

export type RoleFormData = {
  id?: number
  type: RoleType
  name: string
  description: string
  permissions: Permission[]
  vault_id?: number
  subject_type?: SubjectType | null
  subject_id?: number | null
}

type RoleFormDefaults = Partial<AdminRole> | Partial<VaultRole>

type RoleFormProps = {
  type: RoleType
  defaultValues?: RoleFormDefaults
  vaultId?: number
  subjectType?: SubjectType
  subjectId?: number
  onSavedAction?: () => void
}

const sectionVariants = { hidden: { opacity: 0, y: 10 }, visible: { opacity: 1, y: 0 } }

const clonePermission = (permission: Permission, value = permission.value) =>
  new Permission(
    permission.bit_position,
    permission.qualified,
    permission.slug,
    permission.description,
    value,
    permission.id,
    permission.created_at,
    permission.updated_at,
  )

const buildBasePermissions = (permissions: Permission[]): Permission[] =>
  permissions.map(permission => clonePermission(permission, permission.value ?? false))

const mergePermissions = (base: Permission[], existing: Array<Permission | PermissionPayload>): Permission[] => {
  const existingMap = new Map(existing.map(permission => [permission.qualified, permission.value]))
  return base.map(permission => clonePermission(permission, existingMap.get(permission.qualified) ?? false))
}

const toPermissionPayloads = (permissions: Permission[]): PermissionPayload[] =>
  permissions.map(permission => new PermissionPayload(permission.qualified, permission.value))

export default function RoleForm({
  type,
  defaultValues,
  vaultId,
  subjectType,
  subjectId,
  onSavedAction,
}: RoleFormProps) {
  const { permissions, fetchPermissions } = usePermStore()
  const { addAdminRole, updateAdminRole } = useAdminRoleStore()
  const { addVaultRole, updateVaultRole } = useVaultRoleStore()

  const [form, setForm] = useState<RoleFormData>({
    id: defaultValues?.id,
    type,
    name: defaultValues?.name ?? '',
    description: defaultValues?.description ?? '',
    permissions: [],
    vault_id: type === 'vault' ? (vaultId ?? (defaultValues as VaultRole | undefined)?.vault_id) : undefined,
    subject_type:
      type === 'vault' ? (subjectType ?? (defaultValues as VaultRole | undefined)?.subject_type) : undefined,
    subject_id: type === 'vault' ? (subjectId ?? (defaultValues as VaultRole | undefined)?.subject_id) : undefined,
  })

  const [isSubmitting, setIsSubmitting] = useState(false)
  const [isInitializing, setIsInitializing] = useState(true)
  const initializedRef = useRef(false)

  const scopedBackendPermissions = useMemo(
    () => permissions.filter(permission => permission.qualified.startsWith(`${type}.`)),
    [permissions, type],
  )

  useEffect(() => {
    let mounted = true

    const init = async () => {
      setIsInitializing(true)

      if (!permissions.length) await fetchPermissions()
      if (!mounted) return

      const backendPerms = usePermStore
        .getState()
        .permissions.filter(permission => permission.qualified.startsWith(`${type}.`))

      const nextPermissions =
        defaultValues?.permissions?.length ?
          mergePermissions(backendPerms, defaultValues.permissions)
        : buildBasePermissions(backendPerms)

      setForm({
        id: defaultValues?.id,
        type,
        name: defaultValues?.name ?? '',
        description: defaultValues?.description ?? '',
        permissions: nextPermissions,
        vault_id: type === 'vault' ? (vaultId ?? (defaultValues as VaultRole | undefined)?.vault_id) : undefined,
        subject_type:
          type === 'vault' ? (subjectType ?? (defaultValues as VaultRole | undefined)?.subject_type) : undefined,
        subject_id: type === 'vault' ? (subjectId ?? (defaultValues as VaultRole | undefined)?.subject_id) : undefined,
      })

      initializedRef.current = true
      setIsInitializing(false)
    }

    void init()
    return () => {
      mounted = false
    }
  }, [type, defaultValues, vaultId, subjectType, subjectId, permissions.length, fetchPermissions])

  const isEditMode = !!form.id
  const enabledCount = useMemo(() => form.permissions.filter(permission => permission.value).length, [form.permissions])

  const handleSubmit = async () => {
    setIsSubmitting(true)

    try {
      if (type === 'admin') {
        const payload = {
          type: 'admin' as const,
          name: form.name,
          description: form.description,
          permissions: toPermissionPayloads(form.permissions),
          ...(form.id ? { id: form.id } : {}),
        }

        if (form.id) await updateAdminRole(payload)
        else await addAdminRole(payload)
      } else {
        const payload = {
          type: 'vault' as const,
          name: form.name,
          description: form.description,
          permissions: toPermissionPayloads(form.permissions),
          vault_id: form.vault_id!,
          subject_type: form.subject_type,
          subject_id: form.subject_id,
          ...(form.id ? { id: form.id } : {}),
        }

        if (form.id) {
          await updateVaultRole(payload)
        } else {
          if (form.vault_id == null) throw new Error('Vault ID is required for vault roles')
          if (form.subject_type == null) throw new Error('Subject type is required for vault roles')
          if (form.subject_id == null) throw new Error('Subject ID is required for vault roles')
          await addVaultRole(payload)
        }
      }

      onSavedAction?.()
    } finally {
      setIsSubmitting(false)
    }
  }

  const handleReset = () => {
    const basePerms =
      defaultValues?.permissions?.length ?
        mergePermissions(scopedBackendPermissions, defaultValues.permissions)
      : buildBasePermissions(scopedBackendPermissions)

    setForm({
      id: defaultValues?.id,
      type,
      name: defaultValues?.name ?? '',
      description: defaultValues?.description ?? '',
      permissions: basePerms,
      vault_id: type === 'vault' ? (vaultId ?? (defaultValues as VaultRole | undefined)?.vault_id) : undefined,
      subject_type:
        type === 'vault' ? (subjectType ?? (defaultValues as VaultRole | undefined)?.subject_type) : undefined,
      subject_id: type === 'vault' ? (subjectId ?? (defaultValues as VaultRole | undefined)?.subject_id) : undefined,
    })
  }

  if (isInitializing && !initializedRef.current)
    return <div className="rounded-3xl border border-white/10 bg-black/20 p-6 text-white/70">Loading role form...</div>

  return (
    <div className="space-y-6">
      <motion.div variants={sectionVariants} initial="hidden" animate="visible" transition={{ duration: 0.2 }}>
        <div className="rounded-3xl border border-white/10 bg-white/5 p-6 shadow">
          <div className="mb-6 flex items-center justify-between gap-4">
            <div>
              <div className="mb-2">
                <span
                  className={`inline-flex rounded-full border px-2.5 py-1 text-[10px] font-bold tracking-[0.18em] uppercase ${
                    type === 'admin' ?
                      'border-orange-400/20 bg-orange-500/10 text-orange-200'
                    : 'border-cyan-400/20 bg-cyan-500/10 text-cyan-200'
                  }`}>
                  {isEditMode ? `Edit ${type}` : `Create ${type}`}
                </span>
              </div>

              <h2 className="text-2xl font-semibold text-white">{isEditMode ? 'Update Role' : 'Create Role'}</h2>
              <p className="mt-2 text-sm text-white/60">
                Configure metadata and permissions, then submit once like a civilized warlord.
              </p>
            </div>

            <div className="rounded-2xl border border-white/8 bg-black/20 px-4 py-3 text-right">
              <div className="text-[10px] font-bold tracking-[0.18em] text-white/35 uppercase">Enabled</div>
              <div className="mt-1 text-lg font-semibold text-white">
                <span className="text-glow-orange">{enabledCount}</span>
                <span className="text-white/30"> / {form.permissions.length}</span>
              </div>
            </div>
          </div>

          <div className="grid gap-4 lg:grid-cols-2">
            <div className="space-y-2">
              <label className="block text-sm font-medium text-white">Role Name</label>
              <input
                value={form.name}
                onChange={e => setForm(prev => ({ ...prev, name: e.target.value }))}
                className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white"
              />
            </div>

            <div className="space-y-2">
              <label className="block text-sm font-medium text-white">Description</label>
              <input
                value={form.description}
                onChange={e => setForm(prev => ({ ...prev, description: e.target.value }))}
                className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white"
              />
            </div>
          </div>
        </div>
      </motion.div>

      <RolePermissionsForm
        permissions={form.permissions}
        onChangeAction={nextPermissions => setForm(prev => ({ ...prev, permissions: nextPermissions }))}
        title={type === 'admin' ? 'Admin Permissions' : 'Vault Permissions'}
        subtitle="Toggle capabilities for this role. Changes are staged in RoleForm until you submit."
      />

      <div className="flex flex-wrap items-center justify-end gap-3">
        <Button type="button" onClick={handleReset} disabled={isSubmitting}>
          Reset
        </Button>

        <Button type="button" onClick={handleSubmit} disabled={isSubmitting || !form.name.trim()}>
          {isSubmitting ?
            'Saving...'
          : isEditMode ?
            'Update Role'
          : 'Create Role'}
        </Button>
      </div>
    </div>
  )
}
