import { VaultRole, AdminRole, Permission } from '@/models/role'
import { motion } from 'framer-motion'
import { permissionIconMap } from '@/util/icons/permissionIconMap'
import { prettifySnakeCase } from '@/util/prettifySnakeCase'
import Link from 'next/link'
import React from 'react'

type RoleCardProps = { role: VaultRole | AdminRole }

type PermissionGroup = { key: string; label: string; permissions: Permission[] }

const Tooltip = ({ children, label }: { children: React.ReactNode; label: string }) => (
  <div className="group relative flex items-center justify-center">
    {children}
    <div className="pointer-events-none absolute bottom-full left-1/2 z-30 mb-2 hidden w-max max-w-xs -translate-x-1/2 rounded-lg border border-white/10 bg-black/90 px-2 py-1 text-xs text-white shadow-lg group-hover:block">
      {label}
    </div>
  </div>
)

const prettifySegment = (value: string) => value.replace(/[-_]/g, ' ').replace(/\b\w/g, char => char.toUpperCase())

const getPermissionLabel = (permission: Permission) =>
  permission.slug.replace(/[-_]/g, ' ').replace(/\b\w/g, char => char.toUpperCase())

const getPermissionGroupKey = (permission: Permission) => {
  const parts = permission.qualified.split('.')
  return parts.slice(1, -1).join('.')
}

const getPermissionGroupLabel = (groupKey: string) =>
  groupKey.split('.').filter(Boolean).map(prettifySegment).join(' / ')

const groupPermissions = (permissions: Permission[]): PermissionGroup[] => {
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

const getGroupTone = (groupKey: string) => {
  if (groupKey.includes('keys') || groupKey.includes('auth')) return 'yellow'
  if (groupKey.includes('settings') || groupKey.includes('sync')) return 'cyan'
  if (groupKey.includes('files') || groupKey.includes('directories') || groupKey.includes('vaults')) return 'blue'
  if (groupKey.includes('roles') || groupKey.includes('admins')) return 'violet'
  if (groupKey.includes('users') || groupKey.includes('groups') || groupKey.includes('identities')) return 'green'
  if (groupKey.includes('audit') || groupKey.includes('delete') || groupKey.includes('remove')) return 'red'
  return 'orange'
}

const getToneClasses = (tone: string, enabled: boolean) => {
  if (!enabled) return 'border-white/8 bg-white/[0.03] text-white/25'

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

const PermissionIconChip = ({ permission, tone }: { permission: Permission; tone: string }) => {
  const Icon = permissionIconMap[permission.qualified] ?? permissionIconMap[permission.slug]
  const label = `${getPermissionLabel(permission)} — ${permission.description}`

  return (
    <Tooltip label={label}>
      <div
        className={`flex h-11 w-11 items-center justify-center rounded-2xl border transition duration-200 hover:-translate-y-0.5 hover:scale-105 ${getToneClasses(
          tone,
          permission.value,
        )}`}>
        {Icon ?
          <Icon className="fill-current text-lg" aria-label={getPermissionLabel(permission)} />
        : <span className="text-sm font-bold">{getPermissionLabel(permission).charAt(0)}</span>}
      </div>
    </Tooltip>
  )
}

const PermissionCloud = ({ group }: { group: PermissionGroup }) => {
  const tone = getGroupTone(group.key)
  const enabledCount = group.permissions.filter(permission => permission.value).length

  return (
    <section className="rounded-2xl border border-white/8 bg-black/20 p-4">
      <div className="mb-3 flex items-center justify-between gap-3">
        <div className="min-w-0">
          <h4 className="truncate text-[11px] font-bold tracking-[0.22em] text-white/50 uppercase">{group.label}</h4>
        </div>
        <div className="shrink-0 text-[10px] font-semibold text-white/35">
          <span className="text-white/70">{enabledCount}</span> / {group.permissions.length}
        </div>
      </div>

      <div className="flex flex-wrap gap-2">
        {group.permissions.map(permission => (
          <PermissionIconChip key={permission.qualified} permission={permission} tone={tone} />
        ))}
      </div>
    </section>
  )
}

const RoleCard = ({ role }: RoleCardProps) => {
  const permissionGroups = groupPermissions(role.permissions)
  const enabledCount = role.permissions.filter(permission => permission.value).length
  const totalCount = role.permissions.length

  return (
    <Link href={`/roles/${role.id}`} className="block">
      <motion.div
        initial={{ opacity: 0, y: 10 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.28, ease: 'easeOut' }}
        className="group mb-4 rounded-3xl border border-white/10 bg-gradient-to-br from-white/10 to-white/[0.04] p-5 shadow-2xl backdrop-blur-md transition duration-200 hover:scale-[1.01] hover:border-white/15">
        <div className="mb-5 flex flex-col gap-4 md:flex-row md:items-start md:justify-between">
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

            <h3 className="mb-1 truncate text-xl font-semibold text-white">{prettifySnakeCase(role.name)}</h3>
            <p className="max-w-2xl text-sm leading-6 text-white/65">
              {role.description || 'No description provided.'}
            </p>
          </div>

          <div className="shrink-0 rounded-2xl border border-white/8 bg-black/20 px-4 py-3 text-right">
            <div className="text-[10px] font-bold tracking-[0.18em] text-white/40 uppercase">Enabled</div>
            <div className="mt-1 text-lg font-semibold text-white">
              <span className="text-glow-orange">{enabledCount}</span>
              <span className="text-white/35"> / {totalCount}</span>
            </div>
          </div>
        </div>

        <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
          {permissionGroups.length > 0 ?
            permissionGroups.map(group => <PermissionCloud key={group.key} group={group} />)
          : <div className="rounded-2xl border border-white/8 bg-black/20 px-4 py-6 text-sm text-white/45">
              No permissions found.
            </div>
          }
        </div>
      </motion.div>
    </Link>
  )
}

export default RoleCard
