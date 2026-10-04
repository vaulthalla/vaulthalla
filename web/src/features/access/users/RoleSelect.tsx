'use client'

import React, { useMemo } from 'react'
import Link from 'next/link'
import { useWs } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { useSession } from '@/lib/session'
import { Input, Select } from '@/components/ui/Field'
import { Skeleton } from '@/components/ui/State'
import type { AdminRoleRecord } from '@/features/access/types'
import { canAssignRole } from '@/features/access/rbac'
import { roleHref, roleLabel } from '@/features/access/shared'

export const useAdminRoles = () => {
  const canView = useCan({ permission: 'admin.roles.admin.view' })
  const query = useWs('roles.admin.list', null, { enabled: canView, staleTime: 60_000 })
  return { canView, query, roles: (query.data?.roles ?? []) as AdminRoleRecord[] }
}

// The admin role picker for create/edit: every role from roles.admin.list, with the ones this session can't assign
// disabled and labelled with the reason.
export const RoleSelect = ({
  id,
  value,
  onChange,
  verb,
  disabled,
  current,
  invalid,
}: {
  id: string
  value: string
  onChange: (name: string) => void
  verb: 'add' | 'edit'
  disabled?: boolean
  // The account's current role: always selectable (re-saving it is a no-op).
  current?: string
  invalid?: boolean
}) => {
  const actor = useSession(state => state.user)
  const { canView, query, roles } = useAdminRoles()

  const options = useMemo(
    () =>
      [...roles]
        .sort((a, b) => a.name.localeCompare(b.name))
        .map(role => {
          const verdict = role.name === current ? { allowed: true } : canAssignRole(actor, role, verb)
          return { role, verdict }
        }),
    [roles, actor, verb, current],
  )

  if (!canView)
    return (
      <Input
        id={id}
        value={value}
        onChange={event => onChange(event.target.value)}
        disabled={disabled}
        placeholder="Admin role name"
        aria-invalid={invalid || undefined}
      />
    )
  if (query.isPending) return <Skeleton className="h-9 w-full" />

  return (
    <Select id={id} value={value} onChange={event => onChange(event.target.value)} disabled={disabled} aria-invalid={invalid || undefined}>
      {value && !roles.some(r => r.name === value) ? <option value={value}>{roleLabel(value)}</option> : null}
      {!value ? <option value="">Choose a role…</option> : null}
      {options.map(({ role, verdict }) => (
        <option key={role.id} value={role.name} disabled={!verdict.allowed}>
          {roleLabel(role.name)}
          {verdict.allowed ? '' : ` — ${verdict.reason}`}
        </option>
      ))}
    </Select>
  )
}

export const RoleSummary = ({ name }: { name: string }) => {
  const { roles, canView } = useAdminRoles()
  const role = roles.find(r => r.name === name)
  if (!role) return null
  const granted = role.permissions?.filter(p => p.value).length
  const total = role.permissions?.length
  return (
    <span className="text-xs text-fg-subtle">
      {role.description ? `${role.description} ` : ''}
      {granted !== undefined && total ? (
        <span className="tabular">
          ({granted} of {total} admin permissions)
        </span>
      ) : null}
      {canView ? (
        <>
          {' '}
          <Link href={roleHref('admin', role.id)} className="text-accent-text hover:underline">
            View role
          </Link>
        </>
      ) : null}
    </span>
  )
}
