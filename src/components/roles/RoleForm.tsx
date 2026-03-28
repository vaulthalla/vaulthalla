'use client'

import { useEffect, useMemo } from 'react'
import { useForm, SubmitHandler } from 'react-hook-form'
import { motion } from 'framer-motion'
import { Button } from '@/components/Button'
import type { AdminRole, VaultRole } from '@/models/role'

const sectionVariants = { hidden: { opacity: 0, y: 10 }, visible: { opacity: 1, y: 0 } }

const USER_PERMS = [
  { key: 'manage_admins', label: 'Manage Admins' },
  { key: 'manage_users', label: 'Manage Users' },
  { key: 'manage_roles', label: 'Manage Roles' },
  { key: 'manage_settings', label: 'Manage Settings' },
  { key: 'manage_vaults', label: 'Manage Vaults' },
  { key: 'audit_log_access', label: 'Audit Log Access' },
  { key: 'full_api_key_access', label: 'Full API Key Access' },
] as const

const VAULT_PERMS = [
  { key: 'migrate_data', label: 'Migrate Data' },
  { key: 'manage_access', label: 'Manage Access' },
  { key: 'manage_tags', label: 'Manage Tags' },
  { key: 'manage_metadata', label: 'Manage Metadata' },
  { key: 'manage_versions', label: 'Manage Versions' },
  { key: 'manage_file_locks', label: 'Manage File Locks' },
  { key: 'share', label: 'Share Vault' },
  { key: 'sync', label: 'Sync Vault' },
  { key: 'create', label: 'Create Vault' },
  { key: 'download', label: 'Download Files' },
  { key: 'delete', label: 'Delete Files' },
  { key: 'rename', label: 'Rename Files' },
  { key: 'move', label: 'Move Files' },
  { key: 'list', label: 'List Files' },
] as const

export type RoleType = 'user' | 'vault'

export type RoleFormData = { name: string; description: string; permissions: Record<string, boolean>; type: RoleType }

type RoleDefaults =
  | Partial<Pick<AdminRole, 'name' | 'description' | 'permissions'>>
  | Partial<Pick<VaultRole, 'name' | 'description' | 'permissions'>>

export default function RoleForm({
  type,
  defaultValues,
  action,
}: {
  type: RoleType
  defaultValues?: RoleDefaults
  action: SubmitHandler<RoleFormData>
}) {
  const perms = useMemo(() => (type === 'vault' ? VAULT_PERMS : USER_PERMS), [type])

  const { register, handleSubmit, setValue } = useForm<RoleFormData>({
    defaultValues: {
      name: defaultValues?.name ?? '',
      description: defaultValues?.description ?? '',
      permissions: defaultValues?.permissions ?? {},
      type,
    },
  })

  // Ensure form state stays correct if parent switches type (rare, but removes weirdness)
  useEffect(() => {
    setValue('type', type, { shouldDirty: false })
  }, [type, setValue])

  const renderCheckboxGroup = (title: string) => (
    <motion.div
      key={title}
      variants={sectionVariants}
      initial="hidden"
      animate="visible"
      transition={{ duration: 0.2 }}
      className="rounded-xl border border-white/10 bg-white/5 p-4 shadow">
      <h3 className="mb-2 text-sm font-bold tracking-wide text-white/70 uppercase">{title}</h3>
      <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
        {perms.map(p => (
          <label key={p.key} className="flex items-center gap-2 text-sm text-white/90">
            <input type="checkbox" {...register(`permissions.${p.key}`)} className="form-checkbox" />
            {p.label}
          </label>
        ))}
      </div>
    </motion.div>
  )

  return (
    <form onSubmit={handleSubmit(action)} className="space-y-6">
      {/* keep type in payload without making the UI deal with it */}
      <input type="hidden" {...register('type')} />

      <motion.div variants={sectionVariants} initial="hidden" animate="visible" transition={{ duration: 0.2 }}>
        <div className="space-y-2">
          <label className="block text-sm font-medium text-white">Role Name</label>
          <input {...register('name')} className="w-full rounded border border-white/20 bg-black/20 p-2 text-white" />
        </div>

        <div className="mt-4 space-y-2">
          <label className="block text-sm font-medium text-white">Description</label>
          <textarea
            {...register('description')}
            className="w-full rounded border border-white/20 bg-black/20 p-2 text-white"
            rows={2}
          />
        </div>
      </motion.div>

      {renderCheckboxGroup(type === 'vault' ? 'Vault Permissions' : 'User Permissions')}

      <div className="text-right">
        <Button type="submit">Save Role</Button>
      </div>
    </form>
  )
}
