'use client'

import React, { useMemo } from 'react'
import Link from 'next/link'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { useWs, useWsMutation } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { useSession } from '@/lib/session'
import type { IUser } from '@/models/user'
import { formatDateTime } from '@/lib/format'
import { Button } from '@/components/ui/Button'
import { Field, Input } from '@/components/ui/Field'
import { DefinitionList, PageHeader, Panel } from '@/components/ui/Panel'
import { InlineError, QueryState } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { Avatar } from '@/components/shell/UserMenu'
import type { UserRecord } from '@/features/access/types'
import { accountRules } from '@/features/access/rbac'
import { Notice, roleHref, roleLabel } from '@/features/access/shared'
import { passwordPair } from '@/features/access/users/schemas'
import { GroupsPanel, ProfilePanel, VaultAccessPanel } from '@/features/access/users/UserSections'

export function AccountPage() {
  const id = useSession(state => state.user?.id)
  const query = useWs('auth.user.get', { id: id ?? 0 }, { enabled: id !== undefined })
  return (
    <>
      <PageHeader title="Your account" description="Your profile, password and what you have access to." />
      <QueryState query={query}>{data => <Account user={data.user} />}</QueryState>
    </>
  )
}

const Account = ({ user }: { user: UserRecord }) => {
  const actor = useSession(state => state.user)
  const rules = useMemo(() => accountRules(actor, user), [actor, user])
  const canViewRoles = useCan({ permission: 'admin.roles.admin.view' })
  const role = user.admin_role

  return (
    <div className="grid gap-5 lg:grid-cols-[minmax(0,1fr)_20rem]">
      <div className="min-w-0 space-y-5">
        {user.is_protected ? (
          <Notice title="Protected account">
            Your name and email are managed from the server with the vh CLI. You can still change your password here.
          </Notice>
        ) : null}
        <ProfilePanel user={user} rules={rules} quiet={user.is_protected} />
        <PasswordPanel user={user} />
        <GroupsPanel user={user} self readOnly />
        <VaultAccessPanel user={user} />
      </div>
      <div className="min-w-0 space-y-5">
        <Panel>
          <div className="flex items-center gap-3">
            <Avatar name={user.name} />
            <div className="min-w-0">
              <div className="truncate font-medium text-fg">{user.name}</div>
              <div className="truncate text-sm text-fg-subtle">{user.email || 'No email'}</div>
            </div>
          </div>
          <DefinitionList
            className="mt-5"
            items={[
              [
                'Admin role',
                canViewRoles && role ? (
                  <Link key="r" href={roleHref('admin', role.id)} className="text-accent-text hover:underline">
                    {roleLabel(role.name)}
                  </Link>
                ) : (
                  roleLabel(role?.name)
                ),
              ],
              ['Last login', user.last_login ? <span key="l" className="tabular">{formatDateTime(user.last_login)}</span> : 'Never'],
              ['Password set', user.password_changed_at ? <span key="p" className="tabular">{formatDateTime(user.password_changed_at)}</span> : '—'],
              ['Member since', <span key="c" className="tabular">{formatDateTime(user.created_at)}</span>],
              ['CLI login', user.linux_uid !== undefined ? <span key="u" className="tabular">Linux UID {user.linux_uid}</span> : 'Not bound'],
            ]}
          />
          {role?.description ? <p className="mt-4 text-xs text-fg-subtle">{role.description}</p> : null}
        </Panel>
      </div>
    </div>
  )
}

const schema = z
  .object({ current: z.string().min(1, 'Enter your current password'), password: z.string(), confirm: z.string() })
  .superRefine(passwordPair('password', 'confirm'))
type PasswordForm = z.infer<typeof schema>

const PasswordPanel = ({ user }: { user: UserRecord }) => {
  const form = useForm<PasswordForm>({ resolver: zodResolver(schema), defaultValues: { current: '', password: '', confirm: '' } })
  const change = useWsMutation('auth.user.change_password', {
    toPayload: (v: PasswordForm) => ({ id: user.id, old_password: v.current, new_password: v.password }),
    invalidates: ['auth.user.get', 'auth.security.status'],
    onSuccess: data => {
      useSession.setState({ user: data.user as unknown as IUser })
      form.reset()
      notify.success('Password changed')
    },
  })
  const errors = form.formState.errors
  return (
    <Panel id="password" title="Password" description="Use at least 12 characters with upper and lower case, digits and symbols. Weak or breached passwords are refused.">
      <form noValidate onSubmit={form.handleSubmit(values => change.mutate(values))} className="space-y-4">
        {/* Lets password managers attach the new password to the right account. */}
        <input type="text" name="username" autoComplete="username" value={user.name} readOnly hidden />
        <Field label="Current password" htmlFor="pw-current" required error={errors.current?.message} className="sm:max-w-[calc(50%-0.5rem)]">
          <Input id="pw-current" type="password" autoComplete="current-password" aria-invalid={Boolean(errors.current) || undefined} {...form.register('current')} />
        </Field>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="New password" htmlFor="pw-new" required error={errors.password?.message}>
            <Input id="pw-new" type="password" autoComplete="new-password" aria-invalid={Boolean(errors.password) || undefined} {...form.register('password')} />
          </Field>
          <Field label="Confirm new password" htmlFor="pw-confirm" required error={errors.confirm?.message}>
            <Input id="pw-confirm" type="password" autoComplete="new-password" aria-invalid={Boolean(errors.confirm) || undefined} {...form.register('confirm')} />
          </Field>
        </div>
        <InlineError error={change.error} />
        <div className="flex justify-end">
          <Button type="submit" variant="secondary" loading={change.isPending}>
            Change password
          </Button>
        </div>
      </form>
    </Panel>
  )
}
