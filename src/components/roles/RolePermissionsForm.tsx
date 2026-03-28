'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { motion } from 'framer-motion'
import { AdminRole, VaultRole, Permission } from '@/models/role'
import { permissionIconMap } from '@/util/icons/permissionIconMap'
import { prettifySnakeCase } from '@/util/prettifySnakeCase'
import { usePermsStore } from '@/stores/permissionStore'

type RolePermissionsFormProps = { role: AdminRole | VaultRole; title?: string; subtitle?: string; onSaved?: () => void }

type PermissionGroup = { key: string; label: string; permissions: Permission[] }

const prettifySegment = (value: string) => value.replace(/[-_]/g, ' ').replace(/\b\w/g, char => char.toUpperCase())

const getPermissionLabel = (permission: Permission) =>
  permission.slug.replace(/[-_]/g, ' ').replace(/\b\w/g, char => char.toUpperCase())

const getPermissionGroupKey = (permission: Permission) => {
  const parts = permission.qualified.split('.')
  return parts.slice(1, -1).join('.')
}

const getPermissionGroupLabel = (groupKey: string) =>
  groupKey.split('.').filter(Boolean).map(prettifySegment).join(' / ')

const getGroupTone = (groupKey: string) => {
  if (groupKey.includes('keys') || groupKey.includes('auth')) return 'yellow'
  if (groupKey.includes('settings') || groupKey.includes('sync')) return 'cyan'
  if (groupKey.includes('files') || groupKey.includes('directories') || groupKey.includes('vaults')) return 'blue'
  if (groupKey.includes('roles') || groupKey.includes('admins')) return 'violet'
  if (groupKey.includes('users') || groupKey.includes('groups') || groupKey.includes('identities')) return 'green'
  if (groupKey.includes('audit')) return 'red'
  return 'orange'
}

const getToneClasses = (tone: string, enabled: boolean) => {
  if (!enabled)
    return 'border-white/8 bg-white/[0.03] text-white/25 hover:border-white/15 hover:bg-white/[0.05] hover:text-white/50'

  switch (tone) {
    case 'cyan':
      return 'border-cyan-400/25 bg-cyan-400/10 text-glow-cyan shadow-[0_0_20px_rgba(34,211,238,0.18)]'
    case 'blue':
      return 'border-blue-400/25 bg-blue-400/10 text-glow-blue shadow-[0_0_20px_rgba(59,130,246,0.18)]'
    case 'green':
      return 'border-green-400/25 bg-green-400/10 text-glow-green shadow-[0_0_20px_rgba(34,197,94,0.18)]'
    case 'yellow':
      return 'border-yellow-400/25 bg-yellow-400/10 text-glow-yellow shadow-[0_0_20px_rgba(250,204,21,0.18)]'
    case 'red':
      return 'border-red-400/25 bg-red-400/10 text-glow-red shadow-[0_0_20px_rgba(220,38,38,0.18)]'
    case 'violet':
      return 'border-violet-400/25 bg-violet-400/10 text-glow-violet shadow-[0_0_20px_rgba(139,92,246,0.18)]'
    default:
      return 'border-orange-400/25 bg-orange-400/10 text-glow-orange shadow-[0_0_20px_rgba(255,107,0,0.18)]'
  }
}

const groupPermissions = (permissions: Permission[]) => {
  const grouped = new Map<string, Permission[]>()

  for (const permission of permissions) {
    const groupKey = getPermissionGroupKey(permission)
    if (!grouped.has(groupKey)) grouped.set(groupKey, [])
    grouped.get(groupKey)!.push(permission)
  }

  return Array.from(grouped.entries())
    .map(([key, perms]) => ({
      key,
      label: getPermissionGroupLabel(key),
      permissions: [...perms].sort((a, b) => a.bit_position - b.bit_position),
    }))
    .sort((a, b) => a.label.localeCompare(b.label))
}

const ToggleTile = ({
  permission,
  enabled,
  tone,
  onToggle,
}: {
  permission: Permission
  enabled: boolean
  tone: string
  onToggle: () => void
}) => {
  const Icon = permissionIconMap[permission.qualified] ?? permissionIconMap[permission.slug]

  return (
    <button
      type="button"
      onClick={onToggle}
      className={`group flex min-h-28 flex-col items-start rounded-2xl border p-4 text-left transition duration-200 hover:-translate-y-0.5 hover:scale-[1.01] ${getToneClasses(
        tone,
        enabled,
      )}`}>
      <div className="mb-3 flex h-12 w-12 items-center justify-center rounded-2xl border border-current/20 bg-black/10">
        {Icon ?
          <Icon className="fill-current text-2xl" aria-label={getPermissionLabel(permission)} />
        : <span className="text-lg font-bold">{getPermissionLabel(permission).charAt(0)}</span>}
      </div>

      <div className="mb-1 text-sm font-semibold text-white">{getPermissionLabel(permission)}</div>
      <div className="mb-3 text-xs leading-5 text-white/60">{permission.description}</div>

      <div className="mt-auto">
        <span
          className={`inline-flex rounded-full border px-2 py-1 text-[10px] font-bold tracking-[0.16em] uppercase ${
            enabled ? 'border-white/10 bg-white/10 text-white/90' : 'border-white/8 bg-black/20 text-white/35'
          }`}>
          {enabled ? 'enabled' : 'disabled'}
        </span>
      </div>
    </button>
  )
}

const PermissionSection = ({
  group,
  enabledMap,
  onToggle,
}: {
  group: PermissionGroup
  enabledMap: Record<string, boolean>
  onToggle: (qualified: string) => void
}) => {
  const tone = getGroupTone(group.key)
  const enabledCount = group.permissions.filter(permission => enabledMap[permission.qualified]).length

  return (
    <section className="rounded-2xl border border-white/8 bg-black/20 p-4">
      <div className="mb-4 flex items-center justify-between gap-3">
        <div className="min-w-0">
          <h4 className="truncate text-xs font-bold tracking-[0.2em] text-white/50 uppercase">{group.label}</h4>
          <p className="mt-1 text-xs text-white/30">
            {enabledCount} / {group.permissions.length} enabled
          </p>
        </div>
      </div>

      <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
        {group.permissions.map(permission => (
          <ToggleTile
            key={permission.qualified}
            permission={permission}
            enabled={!!enabledMap[permission.qualified]}
            tone={tone}
            onToggle={() => onToggle(permission.qualified)}
          />
        ))}
      </div>
    </section>
  )
}

const RolePermissionsForm = ({
  role,
  title = 'Edit Permissions',
  subtitle = 'Toggle capabilities for this role. Changes are staged locally until you save.',
  onSaved,
}: RolePermissionsFormProps) => {
  const { permissions, fetchPermissions, updateRole } = usePermsStore()

  const [name, setName] = useState(role.name)
  const [description, setDescription] = useState(role.description ?? '')
  const [enabledMap, setEnabledMap] = useState<Record<string, boolean>>({})
  const [isSaving, setIsSaving] = useState(false)

  useEffect(() => {
    if (!permissions.length) void fetchPermissions()
  }, [permissions.length, fetchPermissions])

  useEffect(() => {
    setName(role.name)
    setDescription(role.description ?? '')

    const next: Record<string, boolean> = {}
    for (const permission of role.permissions) next[permission.qualified] = permission.value
    setEnabledMap(next)
  }, [role])

  const scopedPermissions = useMemo(() => {
    const prefix = `${role.type}.`
    return permissions.filter(permission => permission.qualified.startsWith(prefix))
  }, [permissions, role.type])

  const groupedPermissions = useMemo(() => groupPermissions(scopedPermissions), [scopedPermissions])

  const enabledCount = useMemo(() => Object.values(enabledMap).filter(Boolean).length, [enabledMap])

  const togglePermission = (qualified: string) => setEnabledMap(prev => ({ ...prev, [qualified]: !prev[qualified] }))

  const setAllInGroup = (group: PermissionGroup, value: boolean) =>
    setEnabledMap(prev => {
      const next = { ...prev }
      for (const permission of group.permissions) next[permission.qualified] = value
      return next
    })

  const buildUpdatedPermissions = () =>
    scopedPermissions.map(
      permission =>
        new Permission(
          permission.bit_position,
          permission.qualified,
          permission.slug,
          permission.description,
          enabledMap[permission.qualified],
          permission.id,
          permission.created_at ?? null,
          permission.updated_at ?? null,
        ),
    )

  const handleSave = async () => {
    setIsSaving(true)

    try {
      const updatedPermissions = buildUpdatedPermissions()

      // Adjust this payload adapter to match your backend exactly.
      await updateRole({ id: role.id, type: role.type, name, description, permissions: updatedPermissions } as never)

      onSaved?.()
    } finally {
      setIsSaving(false)
    }
  }

  return (
    <motion.div
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.28, ease: 'easeOut' }}
      className="rounded-3xl border border-white/10 bg-linear-to-br from-white/10 to-white/4 p-6 shadow-2xl backdrop-blur-md">
      <div className="mb-6 flex flex-col gap-4 lg:flex-row lg:items-start lg:justify-between">
        <div className="min-w-0">
          <div className="mb-2 flex items-center gap-2">
            <span
              className={`inline-flex rounded-full border px-2.5 py-1 text-[10px] font-bold tracking-[0.18em] uppercase ${
                role.type === 'admin' ?
                  'border-orange-400/20 bg-orange-500/10 text-orange-200'
                : 'border-cyan-400/20 bg-cyan-500/10 text-cyan-200'
              }`}>
              {role.type}
            </span>
          </div>

          <h2 className="text-2xl font-semibold text-white">{title}</h2>
          <p className="mt-2 max-w-3xl text-sm leading-6 text-white/60">{subtitle}</p>
        </div>

        <div className="shrink-0 rounded-2xl border border-white/8 bg-black/20 px-4 py-3 text-right">
          <div className="text-[10px] font-bold tracking-[0.18em] text-white/35 uppercase">Enabled</div>
          <div className="mt-1 text-lg font-semibold text-white">
            <span className="text-glow-orange">{enabledCount}</span>
            <span className="text-white/30"> / {scopedPermissions.length}</span>
          </div>
        </div>
      </div>

      <div className="mb-6 grid gap-4 lg:grid-cols-2">
        <div className="space-y-2">
          <label className="text-xs font-bold tracking-[0.18em] text-white/45 uppercase">Role name</label>
          <input
            value={name}
            onChange={e => setName(e.target.value)}
            className="w-full rounded-2xl border border-white/10 bg-black/20 px-4 py-3 text-white transition outline-none focus:border-cyan-400/30"
          />
        </div>

        <div className="space-y-2">
          <label className="text-xs font-bold tracking-[0.18em] text-white/45 uppercase">Description</label>
          <input
            value={description}
            onChange={e => setDescription(e.target.value)}
            className="w-full rounded-2xl border border-white/10 bg-black/20 px-4 py-3 text-white transition outline-none focus:border-cyan-400/30"
          />
        </div>
      </div>

      <div className="space-y-4">
        {groupedPermissions.map(group => {
          const enabledInGroup = group.permissions.filter(permission => enabledMap[permission.qualified]).length
          const allEnabled = enabledInGroup === group.permissions.length
          const noneEnabled = enabledInGroup === 0

          return (
            <div key={group.key} className="space-y-3">
              <div className="flex flex-wrap items-center justify-between gap-3">
                <div className="text-xs font-bold tracking-[0.2em] text-white/45 uppercase">{group.label}</div>

                <div className="flex items-center gap-2">
                  <button
                    type="button"
                    onClick={() => setAllInGroup(group, true)}
                    className={`rounded-xl border px-3 py-1.5 text-[11px] font-semibold transition ${
                      allEnabled ?
                        'border-green-400/20 bg-green-400/10 text-green-200'
                      : 'border-white/10 bg-black/20 text-white/60 hover:border-white/20 hover:text-white'
                    }`}>
                    Enable all
                  </button>

                  <button
                    type="button"
                    onClick={() => setAllInGroup(group, false)}
                    className={`rounded-xl border px-3 py-1.5 text-[11px] font-semibold transition ${
                      noneEnabled ?
                        'border-red-400/20 bg-red-400/10 text-red-200'
                      : 'border-white/10 bg-black/20 text-white/60 hover:border-white/20 hover:text-white'
                    }`}>
                    Disable all
                  </button>
                </div>
              </div>

              <PermissionSection group={group} enabledMap={enabledMap} onToggle={togglePermission} />
            </div>
          )
        })}
      </div>

      <div className="mt-6 flex flex-wrap items-center justify-end gap-3 border-t border-white/8 pt-6">
        <button
          type="button"
          onClick={() => {
            const reset: Record<string, boolean> = {}
            for (const permission of role.permissions) reset[permission.qualified] = !!permission.value
            setEnabledMap(reset)
            setName(role.name)
            setDescription(role.description ?? '')
          }}
          className="rounded-2xl border border-white/10 bg-black/20 px-4 py-3 text-sm font-semibold text-white/70 transition hover:border-white/20 hover:text-white">
          Reset
        </button>

        <button
          type="button"
          onClick={handleSave}
          disabled={isSaving}
          className="rounded-2xl border border-cyan-400/20 bg-cyan-400/10 px-5 py-3 text-sm font-semibold text-cyan-200 transition hover:border-cyan-400/30 hover:bg-cyan-400/15 disabled:cursor-not-allowed disabled:opacity-50">
          {isSaving ? 'Saving...' : 'Save Permissions'}
        </button>
      </div>
    </motion.div>
  )
}

export default RolePermissionsForm
