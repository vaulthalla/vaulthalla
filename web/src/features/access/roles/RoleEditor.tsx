'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { useParams, useRouter, useSearchParams } from 'next/navigation'
import { useWs, useWsMutation } from '@/lib/query'
import { useCan, isSuperAdminUser } from '@/lib/permissions'
import { useSession } from '@/lib/session'
import { formatDateTime } from '@/lib/format'
import { Badge } from '@/components/ui/Badge'
import { Button } from '@/components/ui/Button'
import { confirm } from '@/components/ui/Confirm'
import { Field, Input, Select } from '@/components/ui/Field'
import { PageHeader, Panel } from '@/components/ui/Panel'
import { EmptyState, InlineError, QueryState } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { TrashIcon } from '@/components/ui/icons'
import type { PermissionRecord } from '@/features/access/types'
import { actorPermissions, isBuiltinRole, roleEditRules } from '@/features/access/rbac'
import { BackLink, DeniedState, Notice, Reason, WithReason, roleHref, roleLabel } from '@/features/access/shared'
import { PermissionMatrix } from '@/features/access/roles/PermissionMatrix'
import { parseRoleType, type RoleType } from '@/features/access/roles/RolesPage'

interface RoleData {
  id: number
  name: string
  description: string
  created_at?: number | string | null
  updated_at?: number | string | null
  permissions?: PermissionRecord[]
}

const listCommand = (type: RoleType) => (type === 'admin' ? 'roles.admin.list' : 'roles.vault.list')

const snapshot = (permissions: PermissionRecord[]) => Object.fromEntries(permissions.map(p => [p.qualified, Boolean(p.value)]))

// /roles/new?type=admin|vault
export function NewRolePage() {
  const params = useSearchParams()
  const type = parseRoleType(params.get('type')) ?? 'admin'
  const canAdd = useCan({ permission: `admin.roles.${type}.add` })
  const canView = useCan({ permission: `admin.roles.${type}.view` })
  // Any role of the type carries the full permission set the server expects in a snapshot.
  const admin = useWs('roles.admin.list', null, { enabled: type === 'admin' && canView })
  const vault = useWs('roles.vault.list', null, { enabled: type === 'vault' && canView })
  const catalog = useWs('permissions.list', null, { enabled: !canView, staleTime: 5 * 60_000 })

  const header = (
    <>
      <BackLink href={`/roles?type=${type}`}>Roles</BackLink>
      <PageHeader eyebrow={`${type === 'admin' ? 'Admin' : 'Vault'} role`} title={`New ${type} role`} />
    </>
  )
  if (!canAdd)
    return (
      <>
        {header}
        <DeniedState what={`create ${type} roles`} />
      </>
    )

  const render = (templates: RoleData[]) => {
    const base = templates[0]?.permissions ?? []
    return <RoleForm type={type} role={null} permissions={base.map(p => ({ ...p, value: false }))} templates={templates} header={header} />
  }
  if (!canView)
    return (
      <QueryState query={catalog}>
        {data =>
          render([
            // Not a real role: just the catalog, so the form knows every permission of this type.
            { id: -1, name: '', description: '', permissions: data.permissions.filter(p => p.qualified.startsWith(`${type}.`)) as unknown as PermissionRecord[] },
          ])
        }
      </QueryState>
    )
  return type === 'admin' ? (
    <QueryState query={admin}>{data => render(data.roles as RoleData[])}</QueryState>
  ) : (
    <QueryState query={vault}>{data => render(data.roles as RoleData[])}</QueryState>
  )
}

// /roles/[type]/[id]
export function EditRolePage() {
  const params = useParams<{ type: string; id: string }>()
  const type = parseRoleType(params.type)
  const id = Number(params.id)
  const canView = useCan({ permission: `admin.roles.${type ?? 'admin'}.view` })
  const valid = Boolean(type) && Number.isInteger(id) && id > 0
  const admin = useWs('role.admin.get', { id }, { enabled: valid && type === 'admin' && canView })
  const vault = useWs('role.vault.get', { id }, { enabled: valid && type === 'vault' && canView })

  if (!valid)
    return (
      <>
        <BackLink href="/roles">Roles</BackLink>
        <EmptyState title="Not found" description="There is no role at this address." />
      </>
    )
  const back = <BackLink href={`/roles?type=${type}`}>Roles</BackLink>
  if (!canView)
    return (
      <>
        {back}
        <DeniedState what={`view ${type} roles`} />
      </>
    )
  const render = (role: RoleData) => (
    <RoleForm key={role.id} type={type!} role={role} permissions={role.permissions ?? []} header={back} />
  )
  return type === 'admin' ? (
    <QueryState query={admin}>{data => render(data.role as RoleData)}</QueryState>
  ) : (
    <QueryState query={vault}>{data => render(data.role as RoleData)}</QueryState>
  )
}

const RoleForm = ({
  type,
  role,
  permissions,
  templates,
  header,
}: {
  type: RoleType
  role: RoleData | null
  // The full permission list for this role type, with the saved values (all false for a new role).
  permissions: PermissionRecord[]
  templates?: RoleData[]
  header: React.ReactNode
}) => {
  const router = useRouter()
  const actor = useSession(state => state.user)
  const creating = !role
  const rules = roleEditRules(actor, type, role)
  const editable = creating || rules.edit.allowed
  const canViewUsers = useCan({ anyOf: ['admin.identities.users.view', 'admin.identities.admins.view'] })
  const users = useWs('auth.users.list', null, { enabled: type === 'admin' && !creating && canViewUsers, staleTime: 60_000 })
  const holders = role && type === 'admin' ? (users.data?.users ?? []).filter(u => u.admin_role?.id === role.id) : []

  const saved = useMemo(() => snapshot(permissions), [permissions])
  const [name, setName] = useState(role?.name ?? '')
  const [description, setDescription] = useState(role?.description ?? '')
  const [values, setValues] = useState<Record<string, boolean>>(saved)
  const [template, setTemplate] = useState('')
  useEffect(() => setValues(saved), [saved])

  const changed = useMemo(() => new Set(permissions.filter(p => Boolean(values[p.qualified]) !== Boolean(saved[p.qualified])).map(p => p.qualified)), [permissions, values, saved])
  const nameChanged = name.trim() !== (role?.name ?? '')
  const descriptionChanged = description.trim() !== (role?.description ?? '')
  const dirty = changed.size > 0 || nameChanged || descriptionChanged

  // The escalation ceiling (core ops::roles::requireWithinCeiling): an admin role can't newly grant what the actor
  // lacks. Vault roles have no ceiling.
  const lockedReason = useMemo(() => {
    if (type !== 'admin' || isSuperAdminUser(actor)) return undefined
    const mine = actorPermissions(actor)
    return (qualified: string) =>
      mine.has(qualified) || saved[qualified] ? undefined : 'You don’t hold this permission, so you can’t grant it.'
  }, [type, actor, saved])

  const permissionPayload = () => permissions.map(p => ({ qualified: p.qualified, value: Boolean(values[p.qualified]) }))
  const invalidates = [listCommand(type), type === 'admin' ? 'role.admin.get' : 'role.vault.get', 'auth.users.list', 'auth.user.get.byName'] as const

  const createAdmin = useWsMutation('role.admin.add', { invalidates: [...invalidates] })
  const createVault = useWsMutation('role.vault.add', { invalidates: [...invalidates] })
  const updateAdmin = useWsMutation('role.admin.update', { invalidates: [...invalidates] })
  const updateVault = useWsMutation('role.vault.update', { invalidates: [...invalidates] })
  const removeAdmin = useWsMutation('role.admin.delete', { invalidates: [...invalidates] })
  const removeVault = useWsMutation('role.vault.delete', { invalidates: [...invalidates] })
  const saving = createAdmin.isPending || createVault.isPending || updateAdmin.isPending || updateVault.isPending
  const removing = removeAdmin.isPending || removeVault.isPending
  const [error, setError] = useState<unknown>(null)

  const save = async () => {
    setError(null)
    const trimmed = name.trim()
    if (!trimmed) {
      setError(new Error('A role needs a name'))
      return
    }
    try {
      if (creating) {
        const payload = { name: trimmed, description: description.trim(), permissions: permissionPayload() }
        const res = type === 'admin' ? await createAdmin.mutateAsync(payload) : await createVault.mutateAsync(payload)
        notify.success(`Created ${roleLabel(res.role.name)}`)
        router.replace(roleHref(type, res.role.id))
        return
      }
      // A patch: name and description only when changed, and the full permission snapshot only when any changed.
      const payload = {
        id: role.id,
        ...(nameChanged ? { name: trimmed } : {}),
        ...(descriptionChanged ? { description: description.trim() } : {}),
        ...(changed.size ? { permissions: permissionPayload() } : {}),
      }
      const res = type === 'admin' ? await updateAdmin.mutateAsync(payload) : await updateVault.mutateAsync(payload)
      notify.success(`Saved ${roleLabel(res.role.name)}`)
    } catch (err) {
      setError(err)
    }
  }

  const discard = () => {
    setName(role?.name ?? '')
    setDescription(role?.description ?? '')
    setValues(saved)
    setTemplate('')
    setError(null)
  }

  const applyTemplate = (value: string) => {
    setTemplate(value)
    const source = templates?.find(t => String(t.id) === value)
    const next = source ? snapshot(source.permissions ?? []) : Object.fromEntries(permissions.map(p => [p.qualified, false]))
    // A template can't smuggle in permissions this session couldn't grant by hand.
    if (lockedReason) for (const q of Object.keys(next)) if (next[q] && lockedReason(q)) next[q] = false
    setValues(next)
  }

  const remove = async () => {
    if (!role) return
    const ok = await confirm({
      title: `Delete ${roleLabel(role.name)}?`,
      description:
        type === 'admin'
          ? 'The role is removed permanently. Accounts must be moved to another role first.'
          : 'The role is removed permanently. It must not be assigned on any vault.',
      confirmLabel: 'Delete role',
      typeToConfirm: role.name,
    })
    if (!ok) return
    try {
      if (type === 'admin') await removeAdmin.mutateAsync({ id: role.id })
      else await removeVault.mutateAsync({ id: role.id })
      notify.success(`Deleted ${roleLabel(role.name)}`)
      router.replace(`/roles?type=${type}`)
    } catch (err) {
      notify.error(err, 'Could not delete the role')
    }
  }

  const deleteBlocked = holders.length
    ? `Assigned to ${holders.length} account${holders.length === 1 ? '' : 's'} (${holders
        .slice(0, 3)
        .map(u => u.name)
        .join(', ')}${holders.length > 3 ? '…' : ''}). Move them to another role first.`
    : undefined

  return (
    <div className="pb-20">
      {header}
      {creating ? null : (
        <PageHeader
          eyebrow={`${type === 'admin' ? 'Admin' : 'Vault'} role`}
          title={roleLabel(role.name)}
          description={
            <span className="flex flex-wrap items-center gap-2">
              <span className="font-mono text-xs text-fg-subtle">{role.name}</span>
              {isBuiltinRole(type, role.name) ? <Badge>Built-in</Badge> : null}
              {role.updated_at ? <span className="text-xs text-fg-faint">Updated {formatDateTime(role.updated_at)}</span> : null}
            </span>
          }
        />
      )}

      {!editable ? (
        <Notice className="mb-5" title="Read only">
          {rules.edit.reason}
        </Notice>
      ) : !creating && isBuiltinRole(type, role.name) ? (
        <Notice className="mb-5" title="Built-in role">
          Changes apply to every {type === 'admin' ? 'account' : 'vault assignment'} that uses this role
          {type === 'vault' && role.name.startsWith('share_') ? ', including share links created with this preset' : ''}.
        </Notice>
      ) : null}

      <div className="space-y-5">
        <Panel title="Details">
          <div className="grid gap-4 md:grid-cols-2">
            <Field label="Name" htmlFor="role-name" required hint="Lowercase with underscores reads best, e.g. support_lead.">
              <Input id="role-name" value={name} onChange={e => setName(e.target.value)} disabled={!editable} autoComplete="off" autoFocus={creating} />
            </Field>
            <Field label="Description" htmlFor="role-description">
              <Input id="role-description" value={description} onChange={e => setDescription(e.target.value)} disabled={!editable} autoComplete="off" />
            </Field>
            {creating && templates?.some(t => t.id > 0) ? (
              <Field
                label="Start from"
                htmlFor="role-template"
                className="md:col-span-2"
                hint="Copies that role’s permissions as a starting point. The new role keeps its own copy.">
                <Select id="role-template" value={template} onChange={e => applyTemplate(e.target.value)}>
                  <option value="">Nothing granted</option>
                  {[...templates]
                    .filter(t => t.id > 0)
                    .sort((a, b) => a.name.localeCompare(b.name))
                    .map(t => (
                      <option key={t.id} value={String(t.id)}>
                        {roleLabel(t.name)}
                      </option>
                    ))}
                </Select>
              </Field>
            ) : null}
          </div>
        </Panel>

        <section aria-labelledby="perm-title">
          <h2 id="perm-title" className="mb-3 text-[15px] font-semibold text-fg">
            Permissions
          </h2>
          {permissions.length ? (
            <PermissionMatrix
              type={type}
              permissions={permissions}
              values={values}
              onChange={changes => setValues(current => ({ ...current, ...changes }))}
              readOnly={!editable}
              lockedReason={lockedReason}
              changed={changed}
            />
          ) : (
            <p className="text-sm text-fg-subtle">The server returned no permissions for this role type.</p>
          )}
        </section>

        {!creating ? (
          <Panel title="Delete role" className="border-danger-line">
            <div className="flex flex-wrap items-center gap-3">
              <WithReason reason={!rules.remove.allowed ? rules.remove.reason : deleteBlocked}>
                <Button variant="danger" onClick={() => void remove()} disabled={!rules.remove.allowed || Boolean(deleteBlocked)} loading={removing}>
                  <TrashIcon aria-hidden />
                  Delete role…
                </Button>
              </WithReason>
              <Reason reason={!rules.remove.allowed ? rules.remove.reason : deleteBlocked} />
            </div>
          </Panel>
        ) : null}
      </div>

      {editable && (creating || dirty || error) ? (
        <div className="glass-strong sticky bottom-4 z-30 mt-6 flex flex-wrap items-center gap-3 rounded-card px-4 py-3">
          <span className="min-w-0 flex-1 text-sm text-fg-subtle">
            {creating ? (
              <>
                <span className="text-fg tabular">{Object.values(values).filter(Boolean).length}</span> permissions granted
              </>
            ) : dirty ? (
              <>
                <span className="text-fg tabular">{changed.size + (nameChanged ? 1 : 0) + (descriptionChanged ? 1 : 0)}</span> unsaved change
                {changed.size + (nameChanged ? 1 : 0) + (descriptionChanged ? 1 : 0) === 1 ? '' : 's'}
              </>
            ) : (
              'No unsaved changes'
            )}
          </span>
          {error ? <InlineError error={error} className="order-last w-full" /> : null}
          {!creating && dirty ? (
            <Button variant="ghost" onClick={discard} disabled={saving}>
              Discard
            </Button>
          ) : null}
          <Button variant="primary" onClick={() => void save()} loading={saving} disabled={!creating && !dirty}>
            {creating ? 'Create role' : 'Save changes'}
          </Button>
        </div>
      ) : null}
    </div>
  )
}
