'use client'

import { replaceQuery } from '@/lib/url'
import React, { useMemo } from 'react'
import Link from 'next/link'
import { useSearchParams } from 'next/navigation'
import { useWs } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { formatDate } from '@/lib/format'
import { Badge } from '@/components/ui/Badge'
import { Button } from '@/components/ui/Button'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { PageHeader } from '@/components/ui/Panel'
import { Meter } from '@/components/ui/Stat'
import { QueryState } from '@/components/ui/State'
import { Segmented } from '@/components/ui/Tabs'
import { PlusIcon } from '@/components/ui/icons'
import { isBuiltinRole } from '@/features/access/rbac'
import { DeniedState, roleHref, roleLabel } from '@/features/access/shared'
import type { PermissionRecord } from '@/features/access/types'

export type RoleType = 'admin' | 'vault'

export interface RoleRow {
  id: number
  name: string
  description: string
  updated_at?: number | string | null
  permissions?: PermissionRecord[]
}

export const parseRoleType = (value: string | null | undefined): RoleType | null =>
  value === 'admin' || value === 'vault' ? value : null

export const PermissionCount = ({ permissions }: { permissions?: PermissionRecord[] }) => {
  if (!permissions?.length) return <span className="text-fg-faint">—</span>
  const granted = permissions.filter(p => p.value).length
  return (
    <span className="flex items-center gap-3">
      <Meter ratio={granted / permissions.length} className="w-16" label={`${granted} of ${permissions.length} permissions`} />
      <span className="whitespace-nowrap text-fg-muted tabular">
        {granted}
        <span className="text-fg-faint"> / {permissions.length}</span>
      </span>
    </span>
  )
}

export function RolesPage() {
  const canViewAdmin = useCan({ permission: 'admin.roles.admin.view' })
  const canViewVault = useCan({ permission: 'admin.roles.vault.view' })
  const canAddAdmin = useCan({ permission: 'admin.roles.admin.add' })
  const canAddVault = useCan({ permission: 'admin.roles.vault.add' })
  const canViewUsers = useCan({ anyOf: ['admin.identities.users.view', 'admin.identities.admins.view'] })

  const params = useSearchParams()
  const type: RoleType = parseRoleType(params.get('type')) ?? (canViewAdmin || !canViewVault ? 'admin' : 'vault')
  const canView = type === 'admin' ? canViewAdmin : canViewVault
  const canAdd = type === 'admin' ? canAddAdmin : canAddVault

  const admin = useWs('roles.admin.list', null, { enabled: type === 'admin' && canViewAdmin })
  const vault = useWs('roles.vault.list', null, { enabled: type === 'vault' && canViewVault })
  const users = useWs('auth.users.list', null, { enabled: type === 'admin' && canViewAdmin && canViewUsers, staleTime: 60_000 })

  // Admin roles are assigned one per account; the user list says how many accounts hold each.
  const holders = useMemo(() => {
    const counts = new Map<number, number>()
    for (const u of users.data?.users ?? []) if (u.admin_role) counts.set(u.admin_role.id, (counts.get(u.admin_role.id) ?? 0) + 1)
    return counts
  }, [users.data])

  const columns = useMemo<Column<RoleRow>[]>(
    () => [
      {
        key: 'name',
        header: 'Role',
        sortValue: r => r.name,
        cell: r => (
          <span className="flex items-center gap-2">
            <span className="truncate">{roleLabel(r.name)}</span>
            {isBuiltinRole(type, r.name) ? <Badge className="h-5 px-2 text-[11px] font-normal">Built-in</Badge> : null}
            {r.permissions?.length ? (
              <span className="text-xs font-normal text-fg-subtle tabular sm:hidden">
                {r.permissions.filter(p => p.value).length}/{r.permissions.length}
              </span>
            ) : null}
          </span>
        ),
      },
      {
        key: 'description',
        header: 'Description',
        hideBelow: 'lg',
        className: 'max-w-[28rem]',
        cell: r => (
          <span className="block truncate text-fg-subtle" title={r.description}>
            {r.description || '—'}
          </span>
        ),
      },
      {
        key: 'permissions',
        header: 'Permissions',
        hideBelow: 'sm',
        sortValue: r => r.permissions?.filter(p => p.value).length ?? null,
        cell: r => <PermissionCount permissions={r.permissions} />,
      },
      ...(type === 'admin' && canViewUsers
        ? [
            {
              key: 'holders',
              header: 'Accounts',
              hideBelow: 'md',
              sortValue: (r: RoleRow) => holders.get(r.id) ?? 0,
              cell: (r: RoleRow) => <span className="text-fg-muted tabular">{users.data ? (holders.get(r.id) ?? 0) : '—'}</span>,
            } as Column<RoleRow>,
          ]
        : []),
      {
        key: 'updated',
        header: 'Updated',
        hideBelow: 'lg',
        sortValue: r => (typeof r.updated_at === 'number' ? r.updated_at : r.updated_at ? Date.parse(r.updated_at) : null),
        cell: r => <span className="whitespace-nowrap text-fg-subtle tabular">{formatDate(r.updated_at)}</span>,
      },
    ],
    [type, canViewUsers, holders, users.data],
  )

  const setType = (next: RoleType) => replaceQuery({ type: next })
  const table = (roles: RoleRow[]) => (
    <DataTable<RoleRow>
      key={type}
      rows={roles}
      columns={columns}
      rowKey={r => r.id}
      rowHref={r => roleHref(type, r.id)}
      initialSort={{ key: 'name', dir: 'asc' }}
      filter={(r, q) => r.name.toLowerCase().includes(q) || roleLabel(r.name).toLowerCase().includes(q) || (r.description ?? '').toLowerCase().includes(q)}
      filterPlaceholder={`Filter ${type} roles`}
    />
  )

  return (
    <>
      <PageHeader
        eyebrow="Access"
        title="Roles"
        description={
          type === 'admin'
            ? 'Admin roles decide what an account can manage across Vaulthalla. Each account holds exactly one.'
            : 'Vault roles decide what an account or group can do inside a vault. Assign them from each vault’s Access tab.'
        }
        actions={
          canAdd ? (
            <Button asChild variant="primary">
              <Link href={`/roles/new?type=${type}`}>
                <PlusIcon aria-hidden />
                New {type} role
              </Link>
            </Button>
          ) : null
        }
      />
      {canViewAdmin || canViewVault ? (
        <div className="mb-4">
          <Segmented
            label="Role type"
            value={type}
            onChange={setType}
            options={[
              { value: 'admin', label: 'Admin roles' },
              { value: 'vault', label: 'Vault roles' },
            ]}
          />
        </div>
      ) : null}
      {!canView ? (
        <DeniedState what={`view ${type} roles`} />
      ) : (
        <>
          {type === 'admin' ? (
            <QueryState query={admin}>{data => table(data.roles)}</QueryState>
          ) : (
            <QueryState query={vault}>{data => table(data.roles)}</QueryState>
          )}
        </>
      )}
    </>
  )
}
