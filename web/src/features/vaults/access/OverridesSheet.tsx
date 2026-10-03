'use client'

import React, { useId, useMemo, useState } from 'react'
import { useWs, useWsMutation } from '@/lib/query'
import { Dialog, SheetContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { Field, Input, Select } from '@/components/ui/Field'
import { Switch } from '@/components/ui/Choice'
import { Segmented } from '@/components/ui/Tabs'
import { InlineError } from '@/components/ui/State'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { TrashIcon } from '@/components/ui/icons'
import { titleCase } from '@/lib/format'
import { isWsError } from '@/lib/ws/errors'
import type { VaultDetail } from '@/features/vaults/model'
import type { VaultRoleOverrideDTO } from '@/util/webSocketCommands'
import { subjectLabel, type AssignmentRow } from '@/features/vaults/access/assignment'

// Path-scoped allow/deny overrides on one assignment: "deny downloads under /finance/**" on top of the role.

const permissionLabel = (qualified: string) => {
  const parts = qualified.replace(/^vault\.fs\./, '').split('.')
  const leaf = parts.pop() ?? qualified
  return `${titleCase(parts.join(' '))}: ${leaf.replaceAll('_', ' ')}`
}

// Daemons before the role.vault.overrides.* commands answer "Unknown command".
const unsupported = (error: unknown) => error instanceof Error && /unknown command/i.test(error.message)

export const OverridesSheet = ({ vault, row, onClose }: { vault: VaultDetail; row: AssignmentRow; onClose: () => void }) => {
  const target = { vault_id: vault.id, subject_type: row.subject.type, subject_id: row.subject.id }
  const overrides = useWs('role.vault.overrides.list', target, { retry: false, select: d => d.overrides ?? [] })

  return (
    <Dialog open onOpenChange={open => !open && onClose()}>
      <SheetContent
        width="max-w-xl"
        title={`Overrides for ${subjectLabel(row)}`}
        description={`On top of the “${row.role.name}” role in ${vault.name}. A matching override wins over the role.`}>
        {overrides.isPending ? (
          <div className="space-y-2">
            <div className="skeleton h-12" />
            <div className="skeleton h-12" />
          </div>
        ) : overrides.error ? (
          unsupported(overrides.error) ? (
            <p className="rounded-control border border-line bg-surface-1 px-3 py-2.5 text-sm text-fg-muted">
              This server can’t manage overrides from the web yet. Use{' '}
              <code className="font-mono text-xs text-fg">vh vault role override</code> on the host, or upgrade Vaulthalla.
            </p>
          ) : isWsError(overrides.error, 'denied') ? (
            <p className="text-sm text-fg-subtle">Your role can’t view overrides for this assignment.</p>
          ) : (
            <InlineError error={overrides.error} />
          )
        ) : (
          <div className="space-y-6">
            <OverrideList target={target} items={overrides.data ?? []} />
            <AddOverride target={target} />
          </div>
        )}
      </SheetContent>
    </Dialog>
  )
}

type Target = { vault_id: number; subject_type: 'user' | 'group'; subject_id: number }

const OverrideList = ({ target, items }: { target: Target; items: VaultRoleOverrideDTO[] }) => {
  const update = useWsMutation('role.vault.overrides.update', { invalidates: ['role.vault.overrides.list'] })
  const remove = useWsMutation('role.vault.overrides.remove', { invalidates: ['role.vault.overrides.list'] })

  if (!items.length) return <p className="text-sm text-fg-subtle">No overrides: the role alone decides.</p>

  const patch = async (item: VaultRoleOverrideDTO, change: { effect?: 'allow' | 'deny'; enabled?: boolean }) => {
    try {
      await update.mutateAsync({ ...target, override_id: item.id, ...change })
    } catch (error) {
      notify.error(error, 'Could not change the override')
    }
  }
  const drop = async (item: VaultRoleOverrideDTO) => {
    const ok = await confirm({
      title: 'Remove this override?',
      description: `${permissionLabel(item.permission.qualified)} on ${item.glob_path} goes back to what the role allows.`,
      confirmLabel: 'Remove override',
    })
    if (!ok) return
    try {
      await remove.mutateAsync({ ...target, override_id: item.id })
      notify.success('Override removed')
    } catch (error) {
      notify.error(error, 'Could not remove the override')
    }
  }

  return (
    <ul className="divide-y divide-line/60 rounded-card border border-line">
      {items.map(item => (
        <li key={item.id} className="flex flex-wrap items-center gap-3 px-3.5 py-3">
          <div className="min-w-0 flex-1">
            <div className="truncate text-sm text-fg" title={item.permission.description}>
              {permissionLabel(item.permission.qualified)}
            </div>
            <div className="truncate font-mono text-xs text-fg-subtle">{item.glob_path || '/'}</div>
          </div>
          <Segmented
            label="Effect"
            value={item.effect}
            onChange={effect => void patch(item, { effect })}
            options={[
              { value: 'allow', label: 'Allow' },
              { value: 'deny', label: 'Deny' },
            ]}
          />
          <Switch aria-label="Override enabled" checked={item.enabled} onCheckedChange={enabled => void patch(item, { enabled })} />
          {item.enabled ? null : <Badge tone="neutral">off</Badge>}
          <button
            type="button"
            aria-label="Remove override"
            onClick={() => void drop(item)}
            className="grid size-8 place-items-center rounded-md text-fg-subtle hover:bg-danger-soft hover:text-danger">
            <TrashIcon className="size-4" aria-hidden />
          </button>
        </li>
      ))}
    </ul>
  )
}

const AddOverride = ({ target }: { target: Target }) => {
  const ids = useId()
  const permissions = useWs('permissions.list', null, {
    select: d => (d.permissions ?? []).filter(p => String(p.qualified).startsWith('vault.fs.')).map(p => ({ qualified: String(p.qualified), description: p.description })),
  })
  const [permission, setPermission] = useState('')
  const [effect, setEffect] = useState<'allow' | 'deny'>('deny')
  const [pattern, setPattern] = useState('/')
  const patternError = pattern.trim() && !pattern.trim().startsWith('/') ? 'Start with / (vault-relative), e.g. /docs/**' : null
  const chosen = useMemo(() => permissions.data?.find(p => p.qualified === permission), [permissions.data, permission])

  const add = useWsMutation('role.vault.overrides.add', {
    invalidates: ['role.vault.overrides.list'],
    onSuccess: () => {
      notify.success('Override added')
      setPermission('')
      setPattern('/')
    },
  })

  return (
    <form
      className="space-y-3.5 rounded-card border border-line bg-surface-1 p-4"
      onSubmit={event => {
        event.preventDefault()
        if (!permission || patternError || !pattern.trim()) return
        add.mutate({ ...target, permissions: [{ qualified: permission, value: effect === 'allow' }], pattern: pattern.trim(), enabled: true })
      }}>
      <h3 className="text-sm font-medium text-fg">Add an override</h3>
      <Field label="Permission" htmlFor={`${ids}-perm`} hint={chosen?.description}>
        <Select id={`${ids}-perm`} value={permission} onChange={e => setPermission(e.target.value)} disabled={permissions.isPending}>
          <option value="">{permissions.isPending ? 'Loading…' : 'Choose a file or folder permission…'}</option>
          {(permissions.data ?? []).map(p => (
            <option key={p.qualified} value={p.qualified}>
              {permissionLabel(p.qualified)}
            </option>
          ))}
        </Select>
      </Field>
      <div className="grid gap-3 sm:grid-cols-[auto_minmax(0,1fr)]">
        <Field label="Effect">
          <Segmented
            label="Effect"
            value={effect}
            onChange={setEffect}
            options={[
              { value: 'deny', label: 'Deny' },
              { value: 'allow', label: 'Allow' },
            ]}
          />
        </Field>
        <Field label="Path pattern" htmlFor={`${ids}-pattern`} error={patternError ?? undefined} hint="Vault-relative glob: /docs/**, /finance/*.csv">
          <Input id={`${ids}-pattern`} className="font-mono" spellCheck={false} value={pattern} onChange={e => setPattern(e.target.value)} aria-invalid={Boolean(patternError)} />
        </Field>
      </div>
      <InlineError error={add.error} />
      <div className="flex justify-end">
        <Button type="submit" variant="subtle" disabled={!permission || Boolean(patternError) || !pattern.trim()} loading={add.isPending}>
          Add override
        </Button>
      </div>
    </form>
  )
}
