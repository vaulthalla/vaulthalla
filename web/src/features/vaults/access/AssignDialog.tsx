'use client'

import React, { useId, useMemo, useState } from 'react'
import { useWs, useWsMutation } from '@/lib/query'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Field, Input, Select } from '@/components/ui/Field'
import { Segmented } from '@/components/ui/Tabs'
import { InlineError } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import type { VaultDetail } from '@/features/vaults/model'
import type { AssignmentRow } from '@/features/vaults/access/assignment'

interface Named {
  id: number
  name: string
  email?: string | null
}

// Assign (or re-assign: the daemon upserts) a vault role to a user or group on this vault.
export const AssignDialog = ({
  open,
  onOpenChange,
  vault,
  existing,
  changing,
  users,
  groups,
  canListUsers,
  canListGroups,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  vault: VaultDetail
  existing: AssignmentRow[]
  changing: AssignmentRow | null
  users: Named[]
  groups: Named[]
  canListUsers: boolean
  canListGroups: boolean
}) => {
  const ids = useId()
  const roles = useWs('roles.vault.list', null, { select: d => d.roles ?? [] })
  const [kind, setKind] = useState<'user' | 'group'>(changing?.subject.type ?? (canListUsers || !canListGroups ? 'user' : 'group'))
  const [subjectId, setSubjectId] = useState<string>(changing ? String(changing.subject.id) : '')
  const [roleId, setRoleId] = useState<string>(changing ? String(changing.role.id) : '')
  const [search, setSearch] = useState('')

  const taken = useMemo(() => new Set(existing.filter(r => r.subject.type === kind).map(r => r.subject.id)), [existing, kind])
  const candidates = useMemo(() => {
    const list = (kind === 'user' ? users : groups).filter(s => !taken.has(s.id))
    const q = search.trim().toLowerCase()
    return q ? list.filter(s => s.name.toLowerCase().includes(q) || (s.email ?? '').toLowerCase().includes(q)) : list
  }, [kind, users, groups, taken, search])
  const role = roles.data?.find(r => String(r.id) === roleId)
  const canList = kind === 'user' ? canListUsers : canListGroups

  const assign = useWsMutation('role.vault.assign', {
    invalidates: ['roles.vault.list.assigned'],
    onSuccess: (_data, input) => {
      const who = changing ? (changing.name ?? `${changing.subject.type} #${changing.subject.id}`) : (kind === 'user' ? users : groups).find(s => s.id === input.subject_id)?.name
      notify.success(changing ? `${who ?? 'Assignment'} now has “${role?.name}”` : `${who ?? 'Subject'} can now reach ${vault.name} as “${role?.name}”`)
      onOpenChange(false)
    },
  })

  const submit = (event: React.FormEvent) => {
    event.preventDefault()
    if (!subjectId || !roleId) return
    assign.mutate({ id: Number(roleId), vault_id: vault.id, subject_type: kind, subject_id: Number(subjectId) })
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent
        title={changing ? `Change ${changing.name ?? 'this'}’s role` : 'Assign a vault role'}
        description={changing ? `The new role replaces “${changing.role.name}” on ${vault.name}. Overrides stay.` : `Give a user or group a role on ${vault.name}.`}
        footer={
          <>
            <Button variant="ghost" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button type="submit" form={`${ids}-form`} variant="primary" disabled={!subjectId || !roleId} loading={assign.isPending}>
              {changing ? 'Change role' : 'Assign'}
            </Button>
          </>
        }>
        <form id={`${ids}-form`} onSubmit={submit} className="space-y-4">
          {changing ? null : (
            <>
              <Segmented
                label="Assign to"
                value={kind}
                onChange={value => {
                  setKind(value)
                  setSubjectId('')
                  setSearch('')
                }}
                options={[
                  { value: 'user', label: 'User' },
                  { value: 'group', label: 'Group' },
                ]}
              />
              {canList ? (
                <Field label={kind === 'user' ? 'User' : 'Group'} htmlFor={`${ids}-subject`} required>
                  <div className="space-y-2">
                    {(kind === 'user' ? users : groups).length > 8 ? (
                      <Input placeholder={`Search ${kind === 'user' ? 'users' : 'groups'}…`} aria-label={`Search ${kind}s`} value={search} onChange={e => setSearch(e.target.value)} />
                    ) : null}
                    <Select id={`${ids}-subject`} value={subjectId} onChange={e => setSubjectId(e.target.value)}>
                      <option value="">{candidates.length ? 'Choose…' : `Every ${kind} already has a role here`}</option>
                      {candidates.map(s => (
                        <option key={s.id} value={s.id}>
                          {s.name}
                          {s.email ? ` · ${s.email}` : ''}
                        </option>
                      ))}
                    </Select>
                  </div>
                </Field>
              ) : (
                <Field label={kind === 'user' ? 'User id' : 'Group id'} htmlFor={`${ids}-subject`} hint={`Your role can’t list ${kind}s, so enter the id.`} required>
                  <Input id={`${ids}-subject`} inputMode="numeric" className="tabular" value={subjectId} onChange={e => setSubjectId(e.target.value.replace(/\D/g, ''))} />
                </Field>
              )}
            </>
          )}
          <Field label="Vault role" htmlFor={`${ids}-role`} required hint={role?.description || undefined}>
            <Select id={`${ids}-role`} value={roleId} onChange={e => setRoleId(e.target.value)} disabled={roles.isPending}>
              <option value="">{roles.isPending ? 'Loading roles…' : 'Choose…'}</option>
              {(roles.data ?? []).map(r => (
                <option key={r.id} value={r.id}>
                  {r.name} ({(r.permissions ?? []).filter(p => p.value).length} permissions)
                </option>
              ))}
            </Select>
          </Field>
          {role ? <PermissionSummary permissions={role.permissions ?? []} /> : null}
          <InlineError error={roles.error ?? assign.error} />
        </form>
      </DialogContent>
    </Dialog>
  )
}

// What the chosen role allows, grouped by area, so nobody assigns blind.
const PermissionSummary = ({ permissions }: { permissions: { qualified: string; value: boolean }[] }) => {
  const groups = new Map<string, string[]>()
  for (const p of permissions) {
    if (!p.value) continue
    const parts = p.qualified.replace(/^vault\.(fs\.)?/, '').split('.')
    const leaf = parts.pop() ?? p.qualified
    const area = parts.join(' ') || 'vault'
    groups.set(area, [...(groups.get(area) ?? []), leaf.replaceAll('_', ' ')])
  }
  if (!groups.size) return <p className="text-xs text-fg-subtle">This role grants no permissions.</p>
  return (
    <dl className="max-h-48 space-y-1.5 overflow-y-auto rounded-control border border-line bg-surface-1 px-3 py-2.5 text-xs">
      {[...groups].map(([area, leaves]) => (
        <div key={area} className="flex gap-3">
          <dt className="w-28 shrink-0 text-fg-subtle capitalize">{area}</dt>
          <dd className="min-w-0 text-fg-muted">{leaves.join(', ')}</dd>
        </div>
      ))}
    </dl>
  )
}
