'use client'

import React, { useMemo, useState } from 'react'
import dynamic from 'next/dynamic'
import { usePathname, useRouter, useSearchParams } from 'next/navigation'
import { useWs, useWsMutation } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { formatDate } from '@/lib/format'
import { Button } from '@/components/ui/Button'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { DropdownMenu, type MenuEntry } from '@/components/ui/Menu'
import { PageHeader } from '@/components/ui/Panel'
import { EmptyState, QueryState } from '@/components/ui/State'
import { EllipsisIcon, PenIcon, PeopleGroupIcon, PlusIcon, TrashIcon, UsersIcon } from '@/components/ui/icons'
import { Avatar } from '@/components/shell/UserMenu'
import type { GroupRecord } from '@/features/access/types'
import { DeniedState } from '@/features/access/shared'
import { GROUP_COMMANDS, deleteGroup } from '@/features/access/groups/groupActions'

const GroupFormDialog = dynamic(() => import('@/features/access/groups/GroupDialogs').then(m => m.GroupFormDialog))
const MembersSheet = dynamic(() => import('@/features/access/groups/GroupDialogs').then(m => m.MembersSheet))

const MemberStack = ({ group }: { group: GroupRecord }) => {
  const shown = group.members.slice(0, 4)
  if (!group.members.length) return <span className="text-fg-faint">No members</span>
  return (
    <span className="flex items-center gap-2">
      <span className="flex -space-x-2">
        {shown.map(m => (
          <span key={m.user.id} title={m.user.name} className="rounded-full ring-2 ring-bg">
            <Avatar name={m.user.name} />
          </span>
        ))}
      </span>
      <span className="text-fg-muted tabular">{group.members.length}</span>
    </span>
  )
}

export function GroupsPage() {
  const canView = useCan({ permission: 'admin.identities.groups.view' })
  const canAdd = useCan({ permission: 'admin.identities.groups.add' })
  const canEdit = useCan({ permission: 'admin.identities.groups.edit' })
  const canDelete = useCan({ permission: 'admin.identities.groups.delete' })
  const query = useWs('groups.list', null, { enabled: canView })
  const remove = useWsMutation('group.remove', { invalidates: [...GROUP_COMMANDS] })

  const router = useRouter()
  const pathname = usePathname()
  const params = useSearchParams()
  const openId = Number(params.get('group')) || null
  const setOpenId = (id: number | null) => router.replace(id ? `${pathname}?group=${id}` : pathname, { scroll: false })

  const [formOpen, setFormOpen] = useState(false)
  const [editing, setEditing] = useState<GroupRecord | null>(null)

  const openForm = (group: GroupRecord | null) => {
    setEditing(group)
    setFormOpen(true)
  }

  const groups = useMemo(() => query.data?.groups ?? [], [query.data])
  const openGroup = groups.find(g => g.id === openId) ?? null

  const columns = useMemo<Column<GroupRecord>[]>(
    () => [
      {
        key: 'name',
        header: 'Name',
        sortValue: g => g.name.toLowerCase(),
        cell: g => (
          <span className="block min-w-0">
            <span className="block truncate font-medium text-fg">{g.name}</span>
            {g.description ? <span className="block max-w-md truncate text-xs font-normal text-fg-subtle">{g.description}</span> : null}
          </span>
        ),
      },
      { key: 'members', header: 'Members', sortValue: g => g.members.length, cell: g => <MemberStack group={g} /> },
      {
        key: 'gid',
        header: 'Linux GID',
        hideBelow: 'md',
        sortValue: g => g.gid ?? null,
        cell: g => (g.gid !== undefined ? <span className="text-fg-muted tabular">{g.gid}</span> : <span className="text-fg-faint">—</span>),
      },
      {
        key: 'created',
        header: 'Created',
        hideBelow: 'lg',
        sortValue: g => (g.created_at ? Date.parse(g.created_at) : null),
        cell: g => <span className="whitespace-nowrap text-fg-subtle tabular">{formatDate(g.created_at)}</span>,
      },
    ],
    [],
  )

  const actions = (group: GroupRecord): MenuEntry[] => [
    { key: 'members', label: 'Members', icon: UsersIcon, onSelect: () => setOpenId(group.id) },
    ...(canEdit
      ? [{ key: 'edit', label: 'Edit…', icon: PenIcon, onSelect: () => openForm(group) } as MenuEntry]
      : []),
    ...(canDelete
      ? (['separator', { key: 'delete', label: 'Delete…', icon: TrashIcon, danger: true, onSelect: () => void deleteGroup(group, id => remove.mutateAsync({ id })) }] as MenuEntry[])
      : []),
  ]

  return (
    <>
      <PageHeader
        eyebrow="Access"
        title="Groups"
        description="Collections of accounts. A vault role granted to a group applies to every member."
        actions={
          canAdd ? (
            <Button variant="primary" onClick={() => openForm(null)}>
              <PlusIcon aria-hidden />
              New group
            </Button>
          ) : null
        }
      />
      {!canView ? (
        <DeniedState what="view groups" />
      ) : (
        <QueryState query={query}>
          {() =>
            groups.length ? (
              <DataTable
                rows={groups}
                columns={columns}
                rowKey={g => g.id}
                onRowClick={g => setOpenId(g.id)}
                initialSort={{ key: 'name', dir: 'asc' }}
                filter={(g, q) => g.name.toLowerCase().includes(q) || (g.description ?? '').toLowerCase().includes(q)}
                filterPlaceholder="Filter groups"
                rowActions={g => (
                  <DropdownMenu
                    label={`Actions for ${g.name}`}
                    entries={actions(g)}
                    trigger={
                      <button
                        type="button"
                        aria-label={`Actions for ${g.name}`}
                        className="grid size-8 place-items-center rounded-md text-fg-subtle hover:bg-surface-3 hover:text-fg">
                        <EllipsisIcon className="size-4" aria-hidden />
                      </button>
                    }
                  />
                )}
              />
            ) : (
              <div className="panel">
                <EmptyState
                  icon={PeopleGroupIcon}
                  title="No groups yet"
                  description="Create a group to grant vault access to several accounts at once."
                  action={
                    canAdd ? (
                      <Button variant="primary" onClick={() => openForm(null)}>
                        <PlusIcon aria-hidden />
                        New group
                      </Button>
                    ) : undefined
                  }
                />
              </div>
            )
          }
        </QueryState>
      )}
      {formOpen ? <GroupFormDialog group={editing} open onOpenChange={setFormOpen} /> : null}
      {openGroup ? <MembersSheet group={openGroup} open onOpenChange={open => !open && setOpenId(null)} /> : null}
    </>
  )
}
