'use client'

import React, { useEffect, useMemo, useState } from 'react'
import Link from 'next/link'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { useWs, useWsMutation } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { useSession } from '@/lib/session'
import type { IUser } from '@/models/user'
import { Badge } from '@/components/ui/Badge'
import { Button } from '@/components/ui/Button'
import { confirm } from '@/components/ui/Confirm'
import { Field, Input, Select } from '@/components/ui/Field'
import { SwitchRow } from '@/components/ui/Choice'
import { Panel } from '@/components/ui/Panel'
import { InlineError, QueryState } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { PeopleGroupIcon, VaultIcon, XmarkIcon } from '@/components/ui/icons'
import { IconButton } from '@/components/ui/IconButton'
import { formatDate } from '@/lib/format'
import type { UserRecord } from '@/features/access/types'
import { vaultRolesOf } from '@/features/access/types'
import type { AccountRules } from '@/features/access/rbac'
import { Reason, roleHref, roleLabel } from '@/features/access/shared'
import { RoleSelect, RoleSummary } from '@/features/access/users/RoleSelect'
import { emailSchema, nameSchema } from '@/features/access/users/schemas'

const USER_COMMANDS = ['auth.users.list', 'auth.user.get.byName', 'auth.user.get'] as const

// After editing your own account, the session copy must follow (the server already swapped its copy).
const syncSession = (user: UserRecord) => {
  if (useSession.getState().user?.id === user.id) useSession.setState({ user: user as unknown as IUser })
}

const profileSchema = z.object({ name: nameSchema, email: emailSchema })
type ProfileForm = z.infer<typeof profileSchema>

export const ProfilePanel = ({
  user,
  rules,
  onRenamed,
  title = 'Profile',
  quiet,
}: {
  user: UserRecord
  rules: Pick<AccountRules, 'profile' | 'rename'>
  onRenamed?: (name: string) => void
  title?: string
  // The page already explains why nothing here can change.
  quiet?: boolean
}) => {
  const form = useForm<ProfileForm>({ resolver: zodResolver(profileSchema), defaultValues: { name: user.name, email: user.email ?? '' } })
  const { reset } = form
  useEffect(() => reset({ name: user.name, email: user.email ?? '' }), [user.name, user.email, reset])

  const update = useWsMutation('auth.user.update', {
    // Only the fields that changed, built explicitly: never the server object (core refuses linux_uid, updated_by…).
    toPayload: (values: ProfileForm) => {
      const name = values.name.trim()
      const email = values.email.trim()
      return {
        id: user.id,
        ...(name !== user.name ? { name } : {}),
        ...(email !== (user.email ?? '') ? { email: email || null } : {}),
      }
    },
    invalidates: [...USER_COMMANDS],
    onSuccess: data => {
      syncSession(data.user)
      notify.success('Profile saved')
      if (data.user.name !== user.name) onRenamed?.(data.user.name)
    },
  })

  const errors = form.formState.errors
  const disabled = !rules.profile.allowed
  return (
    <Panel id="profile" title={title}>
      <form noValidate onSubmit={form.handleSubmit(values => update.mutate(values))} className="space-y-4">
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="Username" htmlFor="profile-name" error={errors.name?.message} hint={rules.profile.allowed ? rules.rename.reason : undefined}>
            <Input id="profile-name" autoComplete="off" disabled={!rules.rename.allowed} aria-invalid={Boolean(errors.name) || undefined} {...form.register('name')} />
          </Field>
          <Field label="Email" htmlFor="profile-email" error={errors.email?.message}>
            <Input id="profile-email" type="email" autoComplete="off" disabled={disabled} placeholder="No email" aria-invalid={Boolean(errors.email) || undefined} {...form.register('email')} />
          </Field>
        </div>
        <InlineError error={update.error} />
        {disabled ? (
          quiet ? null : <Reason reason={rules.profile.reason} />
        ) : (
          <div className="flex justify-end gap-2">
            {form.formState.isDirty ? (
              <Button variant="ghost" onClick={() => reset()}>
                Discard
              </Button>
            ) : null}
            <Button type="submit" variant="secondary" disabled={!form.formState.isDirty} loading={update.isPending}>
              Save profile
            </Button>
          </div>
        )}
      </form>
    </Panel>
  )
}

export const AccessPanel = ({ user, rules, quiet }: { user: UserRecord; rules: AccountRules; quiet?: boolean }) => {
  const currentRole = user.admin_role?.name ?? ''
  const [role, setRole] = useState(currentRole)
  useEffect(() => setRole(currentRole), [currentRole])

  const update = useWsMutation('auth.user.update', { invalidates: [...USER_COMMANDS] })

  const changeRole = async () => {
    const ok = await confirm({
      title: `Make ${user.name} ${roleLabel(role)}?`,
      description: `${user.name} is signed out everywhere and gets the new role's permissions at the next sign-in.`,
      confirmLabel: 'Change role',
      tone: 'primary',
    })
    if (!ok) return
    update.mutate(
      { id: user.id, role },
      {
        onSuccess: () => notify.success(`${user.name} is now ${roleLabel(role)}`),
        onError: error => notify.error(error, 'Could not change the role'),
      },
    )
  }

  const setActive = async (active: boolean) => {
    if (!active) {
      const ok = await confirm({
        title: `Deactivate ${user.name}?`,
        description: `${user.name} is signed out everywhere and can't sign in until reactivated. Their vaults, groups and shares stay as they are.`,
        confirmLabel: 'Deactivate',
      })
      if (!ok) return
    }
    update.mutate(
      { id: user.id, is_active: active },
      {
        onSuccess: () => notify.success(active ? `${user.name} can sign in again` : `${user.name} is deactivated`),
        onError: error => notify.error(error, active ? 'Could not activate the account' : 'Could not deactivate the account'),
      },
    )
  }

  return (
    <Panel id="access" title="Admin role and status" description="The admin role decides what this account can manage across Vaulthalla.">
      <div className="space-y-4">
        <Field label="Admin role" htmlFor="access-role" hint={<RoleSummary name={role} />}>
          <div className="flex flex-col gap-2 sm:flex-row">
            <div className="min-w-0 flex-1">
              <RoleSelect id="access-role" value={role} onChange={setRole} verb="edit" current={currentRole} disabled={!rules.role.allowed || update.isPending} />
            </div>
            {rules.role.allowed ? (
              <Button variant="secondary" onClick={() => void changeRole()} disabled={!role || role === currentRole} loading={update.isPending && update.variables?.role !== undefined}>
                Change role
              </Button>
            ) : null}
          </div>
        </Field>
        {!rules.role.allowed && !quiet ? <Reason reason={rules.role.reason} /> : null}
        <SwitchRow
          id="access-active"
          label="Active"
          hint={
            !rules.active.allowed && !quiet
              ? rules.active.reason
              : user.is_active
                ? rules.active.allowed
                  ? 'The account can sign in. Turning this off signs it out everywhere.'
                  : 'The account can sign in.'
                : `Deactivated${user.deactivated_at ? ` on ${formatDate(user.deactivated_at)}` : ''}. The account can't sign in.`
          }
          checked={user.is_active}
          onCheckedChange={next => void setActive(next)}
          disabled={!rules.active.allowed || update.isPending}
        />
      </div>
    </Panel>
  )
}

export const GroupsPanel = ({ user, self, readOnly }: { user: UserRecord; self: boolean; readOnly?: boolean }) => {
  const canView = useCan({ permission: 'admin.identities.groups.view' })
  const canAdd = useCan({ permission: 'admin.identities.groups.add-member' }) && !readOnly
  const canRemove = useCan({ permission: 'admin.identities.groups.remove-member' }) && !readOnly
  const memberOf = useWs('groups.list.byUser', { user_id: user.id }, { enabled: canView || self })
  const all = useWs('groups.list', null, { enabled: canAdd && canView })
  const [pick, setPick] = useState('')

  const invalidates = ['groups.list.byUser', 'groups.list', 'group.get'] as const
  const add = useWsMutation('group.member.add', {
    invalidates: [...invalidates],
    onSuccess: data => {
      setPick('')
      notify.success(`Added ${user.name} to ${data.group.name}`)
    },
  })
  const remove = useWsMutation('group.member.remove', { invalidates: [...invalidates] })

  const current = useMemo(() => memberOf.data?.groups ?? [], [memberOf.data])
  const available = useMemo(
    () => (all.data?.groups ?? []).filter(g => !current.some(c => c.id === g.id)).sort((a, b) => a.name.localeCompare(b.name)),
    [all.data, current],
  )

  const leave = async (groupId: number, groupName: string) => {
    const ok = await confirm({
      title: `Remove ${user.name} from ${groupName}?`,
      description: `${user.name} loses every vault role granted through ${groupName}.`,
      confirmLabel: 'Remove',
    })
    if (!ok) return
    remove.mutate(
      { group_id: groupId, user_id: user.id },
      {
        onSuccess: () => notify.success(`Removed ${user.name} from ${groupName}`),
        onError: error => notify.error(error, 'Could not remove the membership'),
      },
    )
  }

  if (!canView && !self) return null
  return (
    <Panel id="groups" title="Groups" description="Vault roles granted to a group apply to every member." padded={false}>
      <div className="px-5 pt-3 pb-5">
        <QueryState query={memberOf}>
          {() =>
            current.length ? (
              <ul className="divide-y divide-line/60 rounded-card border border-line text-sm">
                {current.map(group => (
                  <li key={group.id} className="flex items-center gap-3 px-3 py-2">
                    <PeopleGroupIcon className="size-4 shrink-0 text-fg-subtle" aria-hidden />
                    <div className="min-w-0 flex-1">
                      {canView ? (
                        <Link href={`/groups?group=${group.id}`} className="font-medium text-fg hover:text-accent-text">
                          {group.name}
                        </Link>
                      ) : (
                        <span className="font-medium text-fg">{group.name}</span>
                      )}
                      {group.description ? <p className="truncate text-xs text-fg-subtle">{group.description}</p> : null}
                    </div>
                    {canRemove ? (
                      <IconButton
                        size="icon-sm"
                        icon={XmarkIcon}
                        label={`Remove from ${group.name}`}
                        onClick={() => void leave(group.id, group.name)}
                        disabled={remove.isPending}
                      />
                    ) : null}
                  </li>
                ))}
              </ul>
            ) : (
              <p className="text-sm text-fg-subtle">Not a member of any group.</p>
            )
          }
        </QueryState>
        {canAdd && canView ? (
          <form
            className="mt-3 flex flex-col gap-2 sm:flex-row"
            onSubmit={event => {
              event.preventDefault()
              if (pick) add.mutate({ group_id: Number(pick), user_id: user.id })
            }}>
            <div className="min-w-0 flex-1">
              <Select aria-label="Group to add to" value={pick} onChange={event => setPick(event.target.value)} disabled={!available.length}>
                <option value="">{available.length ? 'Add to a group…' : 'No other groups'}</option>
                {available.map(group => (
                  <option key={group.id} value={String(group.id)}>
                    {group.name}
                  </option>
                ))}
              </Select>
            </div>
            <Button type="submit" variant="secondary" disabled={!pick} loading={add.isPending}>
              Add
            </Button>
          </form>
        ) : null}
        <InlineError error={add.error} className="mt-3" />
      </div>
    </Panel>
  )
}

export const VaultAccessPanel = ({ user }: { user: UserRecord }) => {
  const roles = vaultRolesOf(user)
  const canListVaults = useCan({ anyOf: ['admin.vaults.self.view', 'admin.vaults.user.view', 'admin.vaults.admin.view'] })
  const canViewRoles = useCan({ permission: 'admin.roles.vault.view' })
  const vaults = useWs('storage.vault.list', null, { enabled: canListVaults && roles.length > 0, staleTime: 60_000 })
  const groups = useWs('groups.list', null, { enabled: roles.some(r => r.assignment?.subject_type === 'group'), staleTime: 60_000 })
  const vaultName = (id: number) => vaults.data?.vaults.find(v => v.id === id)?.name
  const groupName = (id: number) => groups.data?.groups.find(g => g.id === id)?.name

  return (
    <Panel id="vaults" title="Vault access" description="Vault roles this account holds, directly or through a group. Assign and revoke them from each vault’s Access tab.">
      {roles.length ? (
        <ul className="divide-y divide-line/60 rounded-card border border-line text-sm">
          {roles.map(role => {
            const vaultId = Number(role.assignment?.vault_id)
            const subjectType = role.assignment?.subject_type
            const subjectId = Number(role.assignment?.subject_id)
            return (
              <li key={`${vaultId}-${subjectType}-${subjectId}-${role.id}`} className="flex flex-wrap items-center gap-x-3 gap-y-1 px-3 py-2">
                <VaultIcon className="size-4 shrink-0 text-fg-subtle" aria-hidden />
                <Link href={`/vaults/${vaultId}`} className="min-w-0 flex-1 truncate font-medium text-fg hover:text-accent-text">
                  {vaultName(vaultId) ?? <span className="tabular">Vault #{vaultId}</span>}
                </Link>
                {subjectType === 'group' ? (
                  <span className="text-xs text-fg-subtle">via {groupName(subjectId) ?? `group #${subjectId}`}</span>
                ) : null}
                {canViewRoles ? (
                  <Link href={roleHref('vault', role.id)}>
                    <Badge tone="accent">{roleLabel(role.name)}</Badge>
                  </Link>
                ) : (
                  <Badge tone="accent">{roleLabel(role.name)}</Badge>
                )}
              </li>
            )
          })}
        </ul>
      ) : (
        <p className="text-sm text-fg-subtle">No vault roles assigned to this account or its groups.</p>
      )}
    </Panel>
  )
}
