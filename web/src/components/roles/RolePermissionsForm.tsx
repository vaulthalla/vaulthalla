'use client'

import React, { useMemo } from 'react'
import * as motion from 'motion/react-client'
import { Permission } from '@/models/role'
import { permissionIconMap } from '@/util/icons/permissionIconMap'

type RolePermissionsFormProps = {
  permissions: Permission[]
  onChangeAction: (permissions: Permission[]) => void
  title?: string
  subtitle?: string
}

type PermissionTreeNode = {
  key: string
  segment: string
  path: string
  permissions: Permission[]
  children: PermissionTreeNode[]
}

const prettifySegment = (value: string) => value.replace(/[-_]/g, ' ').replace(/\b\w/g, char => char.toUpperCase())

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

const getPermissionSegments = (permission: Permission): string[] => permission.qualified.split('.').slice(1)

const getPermissionLabel = (permission: Permission) => {
  const segments = getPermissionSegments(permission)
  const leaf = segments.at(-1) ?? permission.slug ?? permission.qualified
  return prettifySegment(leaf)
}

const getNodeLabel = (node: PermissionTreeNode) => prettifySegment(node.segment)

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

const buildPermissionTree = (permissions: Permission[]): PermissionTreeNode[] => {
  type MutableNode = {
    key: string
    segment: string
    path: string
    permissions: Permission[]
    children: Map<string, MutableNode>
  }

  const roots = new Map<string, MutableNode>()

  const getOrCreateNode = (map: Map<string, MutableNode>, segment: string, path: string): MutableNode => {
    let node = map.get(segment)
    if (!node) {
      node = { key: path, segment, path, permissions: [], children: new Map() }
      map.set(segment, node)
    }
    return node
  }

  for (const permission of permissions) {
    const segments = getPermissionSegments(permission)
    if (!segments.length) continue

    let currentMap = roots
    let currentNode: MutableNode | null = null
    let path = ''

    for (const segment of segments.slice(0, -1)) {
      path = path ? `${path}.${segment}` : segment
      currentNode = getOrCreateNode(currentMap, segment, path)
      currentMap = currentNode.children
    }

    if (!currentNode) {
      const rootSegment = segments[0]
      currentNode = getOrCreateNode(roots, rootSegment, rootSegment)
    }

    currentNode.permissions.push(permission)
  }

  const finalize = (map: Map<string, MutableNode>): PermissionTreeNode[] =>
    Array.from(map.values())
      .map(node => ({
        key: node.key,
        segment: node.segment,
        path: node.path,
        permissions: [...node.permissions].sort((a, b) => a.bit_position - b.bit_position),
        children: finalize(node.children),
      }))
      .sort((a, b) => prettifySegment(a.segment).localeCompare(prettifySegment(b.segment)))

  return finalize(roots)
}

const flattenNodePermissions = (node: PermissionTreeNode): Permission[] => [
  ...node.permissions,
  ...node.children.flatMap(flattenNodePermissions),
]

const ToggleTile = ({ permission, tone, onToggle }: { permission: Permission; tone: string; onToggle: () => void }) => {
  const Icon = permissionIconMap[permission.qualified] ?? permissionIconMap[permission.slug]
  const label = getPermissionLabel(permission)
  const description = permission.description ?? ''

  return (
    <button
      type="button"
      onClick={onToggle}
      className={`group flex min-h-28 cursor-pointer flex-col rounded-2xl border p-4 text-left transition duration-200 hover:-translate-y-0.5 hover:scale-[1.01] active:scale-[0.99] ${getToneClasses(
        tone,
        permission.value,
      )}`}>
      <div className="flex w-full items-start justify-between gap-3">
        <div className="flex h-12 w-12 shrink-0 items-center justify-center rounded-2xl border border-current/20 bg-black/10">
          {Icon ?
            <Icon className="fill-current text-2xl" aria-label={label} />
          : <span className="text-lg font-bold">{label.charAt(0)}</span>}
        </div>

        <div className="flex min-w-0 flex-col items-end text-right">
          <span
            className={`mb-1 inline-flex rounded-full border px-2 py-1 text-[10px] font-bold tracking-[0.16em] uppercase ${
              permission.value ?
                'border-white/10 bg-white/10 text-white/90'
              : 'border-white/8 bg-black/20 text-white/35'
            }`}>
            {permission.value ? 'enabled' : 'disabled'}
          </span>

          <div className="mt-1 mr-1 text-sm leading-tight font-semibold text-white">{label}</div>
        </div>
      </div>

      {description && <div className="mt-3 w-full text-xs leading-5 text-white/50">{description}</div>}
    </button>
  )
}

const PermissionTreeSection = ({
  node,
  onToggle,
  onSetAllInNode,
  depth = 0,
}: {
  node: PermissionTreeNode
  onToggle: (qualified: string) => void
  onSetAllInNode: (node: PermissionTreeNode, value: boolean) => void
  depth?: number
}) => {
  const tone = getGroupTone(node.path)
  const label = getNodeLabel(node)
  const descendantPermissions = flattenNodePermissions(node)
  const enabledCount = descendantPermissions.filter(permission => permission.value).length
  const allEnabled = descendantPermissions.length > 0 && enabledCount === descendantPermissions.length
  const noneEnabled = enabledCount === 0

  return (
    <section
      className={`rounded-2xl border border-white/8 bg-black/20 p-4 ${depth === 1 ? 'ml-2 border-white/6 bg-black/15' : ''}`}>
      <div className="mb-4 flex items-center justify-between gap-3">
        <div className="min-w-0">
          <h4 className="truncate text-xs font-bold tracking-[0.2em] text-white/50 uppercase">{label}</h4>
          <p className="mt-1 text-xs text-white/30">
            {enabledCount} / {descendantPermissions.length} enabled
          </p>
        </div>

        <div className="flex items-center gap-2">
          <button
            type="button"
            onClick={() => onSetAllInNode(node, true)}
            className={`rounded-xl border px-3 py-1.5 text-[11px] font-semibold transition ${
              allEnabled ?
                'border-green-400/20 bg-green-400/10 text-green-200'
              : 'border-white/10 bg-black/20 text-white/60 hover:border-white/20 hover:text-white'
            }`}>
            Enable all
          </button>

          <button
            type="button"
            onClick={() => onSetAllInNode(node, false)}
            className={`rounded-xl border px-3 py-1.5 text-[11px] font-semibold transition ${
              noneEnabled ?
                'border-red-400/20 bg-red-400/10 text-red-200'
              : 'border-white/10 bg-black/20 text-white/60 hover:border-white/20 hover:text-white'
            }`}>
            Disable all
          </button>
        </div>
      </div>

      {!!node.permissions.length && (
        <div className="grid gap-3 sm:grid-cols-2 md:grid-cols-3 xl:grid-cols-4">
          {node.permissions.map(permission => (
            <ToggleTile
              key={permission.qualified}
              permission={permission}
              tone={tone}
              onToggle={() => onToggle(permission.qualified)}
            />
          ))}
        </div>
      )}

      {!!node.children.length && (
        <div className={node.permissions.length ? 'mt-4 space-y-4' : 'space-y-4'}>
          {node.children.map(child => (
            <PermissionTreeSection
              key={child.key}
              node={child}
              onToggle={onToggle}
              onSetAllInNode={onSetAllInNode}
              depth={depth + 1}
            />
          ))}
        </div>
      )}
    </section>
  )
}

export default function RolePermissionsForm({
  permissions,
  onChangeAction,
  title = 'Permissions',
  subtitle = 'Toggle capabilities for this role. Changes are staged locally until you save.',
}: RolePermissionsFormProps) {
  const permissionTree = useMemo(() => buildPermissionTree(permissions), [permissions])
  const enabledCount = useMemo(() => permissions.filter(permission => permission.value).length, [permissions])

  const togglePermission = (qualified: string) => {
    onChangeAction(
      permissions.map(permission =>
        permission.qualified === qualified ? clonePermission(permission, !permission.value) : permission,
      ),
    )
  }

  const setAllInNode = (node: PermissionTreeNode, value: boolean) => {
    const qualifiedSet = new Set(flattenNodePermissions(node).map(permission => permission.qualified))

    onChangeAction(
      permissions.map(permission =>
        qualifiedSet.has(permission.qualified) ? clonePermission(permission, value) : permission,
      ),
    )
  }

  return (
    <motion.div
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.28, ease: 'easeOut' }}
      className="rounded-3xl border border-white/10 bg-linear-to-br from-white/10 to-white/4 p-6 shadow-2xl backdrop-blur-md">
      <div className="mb-6 flex flex-col gap-4 lg:flex-row lg:items-start lg:justify-between">
        <div className="min-w-0">
          <h2 className="text-2xl font-semibold text-white">{title}</h2>
          <p className="mt-2 max-w-3xl text-sm leading-6 text-white/60">{subtitle}</p>
        </div>

        <div className="shrink-0 rounded-2xl border border-white/8 bg-black/20 px-4 py-3 text-right">
          <div className="text-[10px] font-bold tracking-[0.18em] text-white/35 uppercase">Enabled</div>
          <div className="mt-1 text-lg font-semibold text-white">
            <span className="text-glow-orange">{enabledCount}</span>
            <span className="text-white/30"> / {permissions.length}</span>
          </div>
        </div>
      </div>

      <div className="space-y-4">
        {permissionTree.map(node => (
          <PermissionTreeSection key={node.key} node={node} onToggle={togglePermission} onSetAllInNode={setAllInNode} />
        ))}
      </div>
    </motion.div>
  )
}
