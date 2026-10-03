'use client'

import React, { useMemo, useState } from 'react'
import { Input, Label } from '@/components/ui/Field'
import { Checkbox, Switch } from '@/components/ui/Choice'
import { Segmented } from '@/components/ui/Tabs'
import { LockIcon, MagnifyingGlassIcon } from '@/components/ui/icons'
import { cn } from '@/util/cn'
import type { PermissionRecord } from '@/features/access/types'
import { groupPermissions, type PermissionGroup } from '@/features/access/roles/permissionCatalog'

type Show = 'all' | 'granted' | 'denied'

const slug = (value: string) => value.replace(/[^a-z0-9]+/gi, '-')

export interface PermissionMatrixProps {
  type: 'admin' | 'vault'
  permissions: PermissionRecord[]
  values: Record<string, boolean>
  onChange: (changes: Record<string, boolean>) => void
  readOnly?: boolean
  // Why a permission can't be granted by this session (it stays revocable); undefined when it can.
  lockedReason?: (qualified: string) => string | undefined
  // Permissions whose value differs from the saved role.
  changed?: Set<string>
}

// A grouped checklist: every permission with a readable label and the server's description, a switch per
// permission, a tri-state bulk toggle per group, and search + granted/not-granted filters.
export const PermissionMatrix = ({ type, permissions, values, onChange, readOnly, lockedReason, changed }: PermissionMatrixProps) => {
  const [search, setSearch] = useState('')
  const [show, setShow] = useState<Show>('all')
  const groups = useMemo(() => groupPermissions(type, permissions), [type, permissions])

  const q = search.trim().toLowerCase()
  const visible = useMemo(
    () =>
      groups
        .map(group => ({
          ...group,
          items: group.items.filter(item => {
            if (show === 'granted' && !values[item.qualified]) return false
            if (show === 'denied' && values[item.qualified]) return false
            if (!q) return true
            return (
              item.label.toLowerCase().includes(q) ||
              item.description.toLowerCase().includes(q) ||
              item.qualified.toLowerCase().includes(q) ||
              group.label.toLowerCase().includes(q)
            )
          }),
        }))
        .filter(group => group.items.length),
    [groups, show, values, q],
  )

  const total = permissions.length
  const granted = permissions.filter(p => values[p.qualified]).length

  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center gap-3">
        <div className="relative w-full sm:w-72">
          <MagnifyingGlassIcon className="pointer-events-none absolute top-1/2 left-3 size-3.5 -translate-y-1/2 text-fg-faint" aria-hidden />
          <Input
            value={search}
            onChange={event => setSearch(event.target.value)}
            placeholder="Search permissions"
            aria-label="Search permissions"
            className="pl-8"
          />
        </div>
        <Segmented
          label="Show permissions"
          value={show}
          onChange={setShow}
          options={[
            { value: 'all', label: 'All' },
            { value: 'granted', label: 'Granted' },
            { value: 'denied', label: 'Not granted' },
          ]}
        />
        <span className="ml-auto text-sm text-fg-subtle tabular">
          <span className="text-fg">{granted}</span> of {total} granted
        </span>
      </div>

      {visible.length ? (
        <div className="grid items-start gap-4 xl:grid-cols-2">
          {visible.map(group => (
            <GroupCard
              key={group.key}
              group={group}
              full={groups.find(g => g.key === group.key)!}
              values={values}
              onChange={onChange}
              readOnly={readOnly}
              lockedReason={lockedReason}
              changed={changed}
            />
          ))}
        </div>
      ) : (
        <p className="panel px-6 py-10 text-center text-sm text-fg-subtle">
          {q ? `No permission matches “${search}”.` : show === 'granted' ? 'This role grants nothing yet.' : 'This role grants everything.'}
        </p>
      )}
    </div>
  )
}

const GroupCard = ({
  group,
  full,
  values,
  onChange,
  readOnly,
  lockedReason,
  changed,
}: {
  group: PermissionGroup
  // The whole group (the bulk toggle acts on all of it, not just the filtered rows).
  full: PermissionGroup
} & Pick<PermissionMatrixProps, 'values' | 'onChange' | 'readOnly' | 'lockedReason' | 'changed'>) => {
  const on = full.items.filter(i => values[i.qualified]).length
  const state = on === 0 ? false : on === full.items.length ? true : 'indeterminate'
  // Bulk grant skips what this session can't grant; bulk revoke always applies.
  const grantable = full.items.filter(i => values[i.qualified] || !lockedReason?.(i.qualified))
  const bulkId = `bulk-${slug(group.key)}`

  const bulk = () => {
    const grant = state !== true
    const changes: Record<string, boolean> = {}
    for (const item of grant ? grantable : full.items) changes[item.qualified] = grant
    onChange(changes)
  }

  return (
    <section className="panel overflow-hidden" aria-labelledby={`${bulkId}-title`}>
      <header className="flex items-start gap-3 border-b border-line px-4 py-3">
        <div className="min-w-0 flex-1">
          <h3 id={`${bulkId}-title`} className="text-sm font-semibold text-fg">
            {group.label}
            <span className="ml-2 text-xs font-normal text-fg-subtle tabular">
              {on}/{full.items.length}
            </span>
          </h3>
          {group.hint ? <p className="mt-0.5 text-xs text-fg-subtle">{group.hint}</p> : null}
        </div>
        {readOnly ? null : (
          <div className="flex shrink-0 items-center gap-2 pt-0.5">
            <Label htmlFor={bulkId} className="text-xs font-normal text-fg-subtle">
              {state === true ? 'Revoke all' : 'Grant all'}
            </Label>
            <Checkbox id={bulkId} checked={state} onCheckedChange={bulk} aria-label={`${state === true ? 'Revoke' : 'Grant'} every ${group.label} permission`} />
          </div>
        )}
      </header>
      <ul className="divide-y divide-line/50">
        {group.items.map(item => {
          const value = Boolean(values[item.qualified])
          const locked = !value ? lockedReason?.(item.qualified) : undefined
          const id = `perm-${slug(item.qualified)}`
          return (
            <li key={item.qualified} className={cn('flex items-start gap-3 px-4 py-2 transition-colors', changed?.has(item.qualified) && 'bg-accent-soft/40')}>
              <div className="min-w-0 flex-1">
                <Label htmlFor={id} className={cn('flex items-center gap-1.5 text-sm', value ? 'text-fg' : 'text-fg-muted')}>
                  {item.label}
                  {changed?.has(item.qualified) ? <span className="size-1.5 rounded-full bg-accent" aria-label="changed" /> : null}
                </Label>
                <p className="mt-0.5 text-xs text-fg-subtle" title={item.qualified}>
                  {item.description}
                </p>
              </div>
              {locked && !readOnly ? (
                // A native title, not a Tooltip: the group card clips overflow.
                <span title={locked} className="mt-0.5 inline-flex items-center gap-1.5 text-fg-faint">
                  <LockIcon className="size-3.5" aria-hidden />
                  <Switch id={id} checked={false} disabled aria-describedby={`${id}-locked`} />
                  <span id={`${id}-locked`} className="sr-only">
                    {locked}
                  </span>
                </span>
              ) : (
                <Switch
                  id={id}
                  className="mt-0.5"
                  checked={value}
                  disabled={readOnly}
                  onCheckedChange={next => onChange({ [item.qualified]: next })}
                />
              )}
            </li>
          )
        })}
      </ul>
    </section>
  )
}
