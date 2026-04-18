import { VaultRole, AdminRole, Permission } from '@/models/role'
import * as motion from 'motion/react-client'
import { permissionIconMap } from '@/util/icons/permissionIconMap'
import { prettifySnakeCase } from '@/util/prettifySnakeCase'
import Link from 'next/link'
import React from 'react'

type RoleCardProps = { role: VaultRole | AdminRole }

type PermissionGroup = { key: string; label: string; permissions: Permission[] }

const MAX_ICONS_PER_GROUP = 8
const MAX_GROUPS = 4

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
    .sort((a, b) => b.permissions.length - a.permissions.length)
}

const getGroupTone = (groupKey: string) => {
  if (groupKey.includes('keys') || groupKey.includes('auth')) return 'yellow'
  if (groupKey.includes('settings') || groupKey.includes('sync')) return 'cyan'
  if (groupKey.includes('files') || groupKey.includes('directories') || groupKey.includes('vaults')) return 'blue'
  if (groupKey.includes('roles') || groupKey.includes('admins')) return 'violet'
  if (groupKey.includes('users') || groupKey.includes('groups') || groupKey.includes('identities')) return 'green'
  if (groupKey.includes('audit')) return 'red'
  return 'orange'
}

const getToneClasses = (tone: string) => {
  switch (tone) {
    case 'cyan':
      return 'border-cyan-400/20 bg-cyan-400/10 text-glow-cyan shadow-[0_0_18px_rgba(34,211,238,0.16)]'
    case 'blue':
      return 'border-blue-400/20 bg-blue-400/10 text-glow-blue shadow-[0_0_18px_rgba(59,130,246,0.16)]'
    case 'green':
      return 'border-green-400/20 bg-green-400/10 text-glow-green shadow-[0_0_18px_rgba(34,197,94,0.16)]'
    case 'yellow':
      return 'border-yellow-400/20 bg-yellow-400/10 text-glow-yellow shadow-[0_0_18px_rgba(250,204,21,0.16)]'
    case 'red':
      return 'border-red-400/20 bg-red-400/10 text-glow-red shadow-[0_0_18px_rgba(220,38,38,0.16)]'
    case 'violet':
      return 'border-violet-400/20 bg-violet-400/10 text-glow-violet shadow-[0_0_18px_rgba(139,92,246,0.16)]'
    default:
      return 'border-orange-400/20 bg-orange-400/10 text-glow-orange shadow-[0_0_18px_rgba(255,107,0,0.16)]'
  }
}

const PermissionIconChip = ({ permission, tone }: { permission: Permission; tone: string }) => {
  const Icon = permissionIconMap[permission.qualified] ?? permissionIconMap[permission.slug]
  const label = `${getPermissionLabel(permission)} — ${permission.description}`

  return (
    <Tooltip label={label}>
      <div
        className={`flex h-8 w-8 items-center justify-center rounded-xl border transition duration-200 hover:-translate-y-0.5 hover:scale-105 ${getToneClasses(
          tone,
        )}`}>
        {Icon ?
          <Icon className="fill-current text-sm" aria-label={getPermissionLabel(permission)} />
        : <span className="text-[10px] font-bold">{getPermissionLabel(permission).charAt(0)}</span>}
      </div>
    </Tooltip>
  )
}

const CompactPermissionGroup = ({ group }: { group: PermissionGroup }) => {
  const tone = getGroupTone(group.key)
  const shown = group.permissions.slice(0, MAX_ICONS_PER_GROUP)
  const remaining = group.permissions.length - shown.length

  return (
    <div className="flex items-start gap-4">
      {/* LEFT: label */}
      <div className="w-44 shrink-0 pt-1">
        <div className="text-[10px] leading-tight font-bold tracking-[0.18em] text-white/45 uppercase">
          {group.label}
        </div>
        <div className="mt-1 text-[10px] text-white/30">{group.permissions.length}</div>
      </div>

      {/* RIGHT: icons stretch */}
      <div className="flex flex-wrap gap-1.5">
        {shown.map(permission => (
          <PermissionIconChip key={permission.qualified} permission={permission} tone={tone} />
        ))}

        {remaining > 0 && (
          <div className="flex h-8 min-w-8 items-center justify-center rounded-xl border border-white/10 bg-white/[0.04] px-2 text-[10px] font-semibold text-white/45">
            +{remaining}
          </div>
        )}
      </div>
    </div>
  )
}

const RoleCard = ({ role }: RoleCardProps) => {
  const enabledPermissions = role.permissions.filter(permission => permission.value)
  const permissionGroups = groupPermissions(enabledPermissions).slice(0, MAX_GROUPS)
  const hiddenGroups = Math.max(0, groupPermissions(enabledPermissions).length - permissionGroups.length)
  const enabledCount = enabledPermissions.length
  const totalCount = role.permissions.length

  return (
    <Link href={`/roles/${role.type}/${role.id}`} className="block">
      <motion.div
        initial={{ opacity: 0, y: 10 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.24, ease: 'easeOut' }}
        className="group mb-4 rounded-3xl border border-white/10 bg-gradient-to-br from-white/10 to-white/[0.04] p-5 shadow-2xl backdrop-blur-md transition duration-200 hover:scale-[1.01] hover:border-white/15">
        <div className="mb-4 flex items-start justify-between gap-4">
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

            <h3 className="mb-1 truncate text-lg font-semibold text-white">{prettifySnakeCase(role.name)}</h3>
            <p className="line-clamp-2 max-w-2xl text-sm leading-5 text-white/60">
              {role.description || 'No description provided.'}
            </p>
          </div>

          <div className="shrink-0 rounded-2xl border border-white/8 bg-black/20 px-3 py-2 text-right">
            <div className="text-[10px] font-bold tracking-[0.16em] text-white/35 uppercase">Enabled</div>
            <div className="mt-1 text-base font-semibold text-white">
              <span className="text-glow-orange">{enabledCount}</span>
              <span className="text-white/30"> / {totalCount}</span>
            </div>
          </div>
        </div>

        {permissionGroups.length > 0 ?
          <div className="space-y-2 divide-y divide-white/5">
            {permissionGroups.map(group => (
              <CompactPermissionGroup key={group.key} group={group} />
            ))}
            {hiddenGroups > 0 && (
              <div className="pt-1 text-[11px] font-medium text-white/35">
                + {hiddenGroups} more permission group{hiddenGroups === 1 ? '' : 's'}
              </div>
            )}
          </div>
        : <div className="rounded-2xl border border-white/8 bg-black/20 px-4 py-4 text-sm text-white/40">
            No enabled permissions.
          </div>
        }
      </motion.div>
    </Link>
  )
}

export default RoleCard
