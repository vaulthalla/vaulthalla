'use client'

import React, { useMemo, useState } from 'react'
import Link from 'next/link'
import { useWs } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { useSession } from '@/lib/session'
import { formatDate, formatDateTime, formatRelative } from '@/lib/format'
import { Button } from '@/components/ui/Button'
import { Label } from '@/components/ui/Field'
import { Checkbox } from '@/components/ui/Choice'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { PageHeader } from '@/components/ui/Panel'
import { QueryState } from '@/components/ui/State'
import { UserPlusIcon } from '@/components/ui/icons'
import { Avatar } from '@/components/shell/UserMenu'
import { AccountBadges, DeniedState, StatusBadge, roleLabel, userHref } from '@/features/access/shared'
import type { UserRecord } from '@/features/access/types'

const VIEW = ['admin.identities.users.view', 'admin.identities.admins.view']
const ADD = ['admin.identities.users.add', 'admin.identities.admins.add']

export function UsersPage() {
  const canView = useCan({ anyOf: VIEW })
  const canAdd = useCan({ anyOf: ADD })
  const selfId = useSession(state => state.user?.id)
  const query = useWs('auth.users.list', null, { enabled: canView })
  const [showSystem, setShowSystem] = useState(false)

  const columns = useMemo<Column<UserRecord>[]>(
    () => [
      {
        key: 'name',
        header: 'Name',
        sortValue: u => u.name.toLowerCase(),
        cell: u => (
          <span className="flex min-w-0 items-center gap-3">
            <Avatar name={u.name} />
            <span className="min-w-0">
              <span className="block truncate" title={u.name}>
                {u.name}
              </span>
            </span>
            <AccountBadges user={u} selfId={selfId} className="hidden xl:inline-flex" />
          </span>
        ),
      },
      {
        key: 'email',
        header: 'Email',
        hideBelow: 'md',
        sortValue: u => u.email || null,
        className: 'max-w-[18rem]',
        cell: u =>
          u.email ? (
            <span className="block truncate text-fg-muted" title={u.email}>
              {u.email}
            </span>
          ) : (
            <span className="text-fg-faint">—</span>
          ),
      },
      {
        key: 'role',
        header: 'Admin role',
        sortValue: u => u.admin_role?.name ?? null,
        cell: u => <span className="whitespace-nowrap text-fg-muted">{roleLabel(u.admin_role?.name)}</span>,
      },
      {
        key: 'status',
        header: 'Status',
        sortValue: u => (u.is_active ? 0 : 1),
        cell: u => <StatusBadge active={u.is_active} />,
      },
      {
        key: 'last_login',
        header: 'Last login',
        hideBelow: 'sm',
        sortValue: u => (u.last_login ? Date.parse(u.last_login) : null),
        cell: u =>
          u.last_login ? (
            <span className="whitespace-nowrap text-fg-muted tabular" title={formatDateTime(u.last_login)}>
              {formatRelative(u.last_login)}
            </span>
          ) : (
            <span className="text-fg-faint">Never</span>
          ),
      },
      {
        key: 'created',
        header: 'Created',
        hideBelow: 'lg',
        sortValue: u => (u.created_at ? Date.parse(u.created_at) : null),
        cell: u => <span className="whitespace-nowrap text-fg-subtle tabular">{formatDate(u.created_at)}</span>,
      },
    ],
    [selfId],
  )

  return (
    <>
      <PageHeader
        eyebrow="Access"
        title="Users"
        description="Accounts that can sign in to the console and the vh CLI. An account's admin role decides what it can manage; vault roles decide what it can do inside each vault."
        actions={
          canAdd ? (
            <Button asChild variant="primary">
              <Link href="/users/new">
                <UserPlusIcon aria-hidden />
                New user
              </Link>
            </Button>
          ) : null
        }
      />
      {!canView ? (
        <DeniedState what="view users" />
      ) : (
        <QueryState query={query}>
          {data => {
            const systemCount = data.users.filter(u => u.system_only).length
            const rows = showSystem ? data.users : data.users.filter(u => !u.system_only)
            return (
              <DataTable
                rows={rows}
                columns={columns}
                rowKey={u => u.id}
                rowHref={u => userHref(u.name)}
                initialSort={{ key: 'name', dir: 'asc' }}
                filter={(u, q) =>
                  u.name.toLowerCase().includes(q) ||
                  (u.email ?? '').toLowerCase().includes(q) ||
                  (u.admin_role?.name ?? '').toLowerCase().includes(q)
                }
                filterPlaceholder="Filter by name, email or role"
                empty={canAdd ? 'No users yet. Create the first one.' : 'No users to show.'}
                toolbar={
                  systemCount ? (
                    <div className="flex items-center gap-2">
                      <Checkbox id="show-system" checked={showSystem} onCheckedChange={v => setShowSystem(v === true)} />
                      <Label htmlFor="show-system" className="font-normal text-fg-subtle">
                        Show system accounts <span className="tabular">({systemCount})</span>
                      </Label>
                    </div>
                  ) : null
                }
              />
            )
          }}
        </QueryState>
      )}
    </>
  )
}
