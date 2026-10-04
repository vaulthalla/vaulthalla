'use client'

import React, { useMemo, useState } from 'react'
import Link from 'next/link'
import { useWs } from '@/lib/query'
import { api } from '@/lib/session'
import { invalidate } from '@/lib/query'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { DropdownMenu } from '@/components/ui/Menu'
import { EmptyState, QueryState } from '@/components/ui/State'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import {
  BanIcon,
  EllipsisIcon,
  PeopleGroupIcon,
  ShieldKeyholeIcon,
  SlidersIcon,
  UserIcon,
  UserPlusIcon,
  ArrowRightArrowLeftIcon,
} from '@/components/ui/icons'
import { formatDate } from '@/lib/format'
import { useCurrentVault } from '@/features/vaults/VaultShell'
import { useGroupNames, useUserNames } from '@/features/vaults/hooks'
import { assignmentSubject } from '@/features/vaults/model'
import dynamic from 'next/dynamic'

// Dialog code (Radix) loads only when one opens.
const AssignDialog = dynamic(() => import('@/features/vaults/access/AssignDialog').then(m => m.AssignDialog), { ssr: false })
const OverridesSheet = dynamic(() => import('@/features/vaults/access/OverridesSheet').then(m => m.OverridesSheet), { ssr: false })
import { subjectLabel, type AssignmentRow } from '@/features/vaults/access/assignment'

const plural = (n: number, word: string) => `${n} ${word}${n === 1 ? '' : 's'}`

export const VaultAccess = () => {
  const vault = useCurrentVault()
  const assigned = useWs('roles.vault.list.assigned', { id: vault.id }, { select: d => d.assigned_roles ?? [] })
  const users = useUserNames()
  const groups = useGroupNames()
  const [assignOpen, setAssignOpen] = useState(false)
  const [changing, setChanging] = useState<AssignmentRow | null>(null)
  const [overridesFor, setOverridesFor] = useState<AssignmentRow | null>(null)

  const rows = useMemo<AssignmentRow[]>(
    () =>
      (assigned.data ?? []).flatMap(role => {
        const subject = assignmentSubject(role)
        if (!subject) return []
        const name = (subject.type === 'user' ? users.names : groups.names).get(subject.id) ?? null
        return [{ subject, name, role }]
      }),
    [assigned.data, users.names, groups.names],
  )

  const unassign = async (row: AssignmentRow) => {
    const ok = await confirm({
      title: `Remove ${subjectLabel(row)}’s access?`,
      description: `${subjectLabel(row)} loses the “${row.role.name}” role on ${vault.name}, and any permission overrides on that assignment. Access through other roles or groups is unaffected.`,
      confirmLabel: 'Remove access',
    })
    if (!ok) return
    try {
      await api.send('role.vault.unassign', { vault_id: vault.id, subject_type: row.subject.type, subject_id: row.subject.id })
      await invalidate('roles.vault.list.assigned')
      notify.success(`${subjectLabel(row)} no longer has a role on ${vault.name}`)
    } catch (error) {
      notify.error(error, 'Could not remove the assignment')
    }
  }

  const columns: Column<AssignmentRow>[] = [
    {
      key: 'subject',
      header: 'Who',
      sortValue: r => subjectLabel(r).toLowerCase(),
      cell: r => {
        const Icon = r.subject.type === 'user' ? UserIcon : PeopleGroupIcon
        const label = subjectLabel(r)
        const href = r.subject.type === 'user' ? (r.name ? `/users/${encodeURIComponent(r.name)}` : null) : '/groups'
        return (
          <span className="flex min-w-0 items-center gap-2.5">
            <span className="grid size-7 shrink-0 place-items-center rounded-full border border-line bg-surface-2 text-fg-subtle [&_svg]:size-3.5" aria-hidden>
              <Icon />
            </span>
            {href ? (
              <Link href={href} className="truncate font-medium text-fg hover:text-accent-text">
                {label}
              </Link>
            ) : (
              <span className={r.name ? 'truncate font-medium text-fg' : 'truncate text-fg-subtle tabular'}>{label}</span>
            )}
          </span>
        )
      },
    },
    {
      key: 'type',
      header: 'Type',
      hideBelow: 'sm',
      sortValue: r => r.subject.type,
      cell: r => <Badge tone="neutral">{r.subject.type === 'user' ? 'User' : 'Group'}</Badge>,
    },
    {
      key: 'role',
      header: 'Vault role',
      sortValue: r => r.role.name,
      cell: r => {
        const enabled = (r.role.permissions ?? []).filter(p => p.value).length
        return (
          <span className="inline-flex min-w-0 items-center gap-2" title={r.role.description || undefined}>
            <ShieldKeyholeIcon className="size-3.5 shrink-0 text-accent-text" aria-hidden />
            <span className="truncate text-fg">{r.role.name}</span>
            <span className="shrink-0 text-xs text-fg-subtle tabular">{enabled} perms</span>
          </span>
        )
      },
    },
    {
      key: 'assigned',
      header: 'Assigned',
      hideBelow: 'md',
      sortValue: r => String(r.role.assigned_at ?? ''),
      cell: r => <span className="text-fg-subtle tabular">{formatDate(r.role.assigned_at)}</span>,
    },
  ]

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <p className="max-w-2xl text-sm text-fg-subtle">
          Users and groups with a vault role here. A role sets what they can do; overrides allow or deny single permissions under a path.
        </p>
        <Button variant="primary" onClick={() => setAssignOpen(true)}>
          <UserPlusIcon aria-hidden />
          Assign role
        </Button>
      </div>

      <QueryState query={assigned}>
        {() =>
          rows.length ? (
            <DataTable
              rows={rows}
              columns={columns}
              rowKey={r => `${r.subject.type}-${r.subject.id}`}
              initialSort={{ key: 'subject', dir: 'asc' }}
              filter={(r, q) => [subjectLabel(r), r.role.name, r.subject.type].some(t => t.toLowerCase().includes(q))}
              filterPlaceholder="Filter people and roles…"
              toolbar={
                <span className="text-xs text-fg-subtle tabular">
                  {plural(rows.filter(r => r.subject.type === 'user').length, 'user')} · {plural(rows.filter(r => r.subject.type === 'group').length, 'group')}
                </span>
              }
              rowActions={r => (
                <DropdownMenu
                  label={`Actions for ${subjectLabel(r)}`}
                  entries={[
                    { key: 'overrides', label: 'Permission overrides…', icon: SlidersIcon, onSelect: () => setOverridesFor(r) },
                    { key: 'change', label: 'Change role…', icon: ArrowRightArrowLeftIcon, onSelect: () => setChanging(r) },
                    'separator',
                    { key: 'unassign', label: 'Remove access…', icon: BanIcon, danger: true, onSelect: () => void unassign(r) },
                  ]}
                  trigger={
                    <button type="button" aria-label={`Actions for ${subjectLabel(r)}`} className="grid size-8 place-items-center rounded-md text-fg-subtle hover:bg-surface-3 hover:text-fg">
                      <EllipsisIcon className="size-4" aria-hidden />
                    </button>
                  }
                />
              )}
            />
          ) : (
            <div className="panel">
              <EmptyState
                icon={UserPlusIcon}
                title="Nobody has a role on this vault yet"
                description="The owner and administrators can always reach it. Assign a vault role to give other users or groups access."
                action={
                  <Button variant="subtle" onClick={() => setAssignOpen(true)}>
                    Assign a role
                  </Button>
                }
              />
            </div>
          )
        }
      </QueryState>

      {assignOpen || changing ? (
      <AssignDialog
        open
        onOpenChange={open => {
          if (!open) {
            setAssignOpen(false)
            setChanging(null)
          }
        }}
        vault={vault}
        existing={rows}
        changing={changing}
        users={users.users}
        groups={groups.groups}
        canListUsers={users.canList}
        canListGroups={groups.canList}
      />
      ) : null}
      {overridesFor ? <OverridesSheet vault={vault} row={overridesFor} onClose={() => setOverridesFor(null)} /> : null}
    </div>
  )
}
