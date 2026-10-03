'use client'

import React, { useEffect } from 'react'
import { useRouter } from 'next/navigation'
import { Controller, useForm, useWatch } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { useWsMutation } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { Button } from '@/components/ui/Button'
import { Field, Input } from '@/components/ui/Field'
import { SwitchRow } from '@/components/ui/Choice'
import { PageHeader, Panel } from '@/components/ui/Panel'
import { InlineError } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { BackLink, DeniedState, userHref } from '@/features/access/shared'
import { RoleSelect, RoleSummary, useAdminRoles } from '@/features/access/users/RoleSelect'
import { emailSchema, nameSchema, passwordPair } from '@/features/access/users/schemas'

const schema = z
  .object({
    name: nameSchema,
    email: emailSchema,
    role: z.string().trim().min(1, 'Choose an admin role'),
    password: z.string(),
    confirm: z.string(),
    is_active: z.boolean(),
  })
  .superRefine(passwordPair('password', 'confirm'))

type Form = z.infer<typeof schema>

export function NewUserPage() {
  const canAdd = useCan({ anyOf: ['admin.identities.users.add', 'admin.identities.admins.add'] })
  const router = useRouter()
  const { roles } = useAdminRoles()
  const form = useForm<Form>({
    resolver: zodResolver(schema),
    defaultValues: { name: '', email: '', role: '', password: '', confirm: '', is_active: true },
  })
  const { register, handleSubmit, control, formState, setValue, getValues } = form
  const role = useWatch({ control, name: 'role' })
  const active = useWatch({ control, name: 'is_active' })

  // Default to the unprivileged role once the list arrives.
  useEffect(() => {
    if (!getValues('role') && roles.some(r => r.name === 'unprivileged')) setValue('role', 'unprivileged')
  }, [roles, getValues, setValue])

  const create = useWsMutation('auth.register', {
    toPayload: (values: Form) => ({
      name: values.name.trim(),
      ...(values.email.trim() ? { email: values.email.trim() } : {}),
      password: values.password,
      is_active: values.is_active,
      role: values.role.trim(),
    }),
    invalidates: ['auth.users.list'],
    onSuccess: data => {
      notify.success(`Created ${data.user.name}`, data.user.is_active ? undefined : 'The account is inactive and can’t sign in yet.')
      router.push(userHref(data.user.name))
    },
  })

  if (!canAdd)
    return (
      <>
        <BackLink href="/users">Users</BackLink>
        <PageHeader title="New user" />
        <DeniedState what="create users" />
      </>
    )

  return (
    <div className="max-w-3xl">
      <BackLink href="/users">Users</BackLink>
      <PageHeader title="New user" description="The account signs in with this name and password. You can add it to groups and vaults afterwards." />
      <form onSubmit={handleSubmit(values => create.mutate(values))} noValidate className="space-y-5">
        <Panel title="Profile">
          <div className="grid gap-4 sm:grid-cols-2">
            <Field label="Username" htmlFor="new-name" required error={formState.errors.name?.message} hint="3 to 50 characters.">
              <Input id="new-name" autoComplete="off" autoFocus aria-invalid={Boolean(formState.errors.name) || undefined} {...register('name')} />
            </Field>
            <Field label="Email" htmlFor="new-email" error={formState.errors.email?.message} hint="Optional.">
              <Input id="new-email" type="email" autoComplete="off" aria-invalid={Boolean(formState.errors.email) || undefined} {...register('email')} />
            </Field>
          </div>
        </Panel>

        <Panel title="Password" description="Share it with the person over a trusted channel. They can change it from their account page.">
          <div className="grid gap-4 sm:grid-cols-2">
            <Field
              label="Password"
              htmlFor="new-password"
              required
              error={formState.errors.password?.message}
              hint="At least 12 characters with upper and lower case, digits and symbols. Weak or breached passwords are refused.">
              <Input id="new-password" type="password" autoComplete="new-password" aria-invalid={Boolean(formState.errors.password) || undefined} {...register('password')} />
            </Field>
            <Field label="Confirm password" htmlFor="new-confirm" required error={formState.errors.confirm?.message}>
              <Input id="new-confirm" type="password" autoComplete="new-password" aria-invalid={Boolean(formState.errors.confirm) || undefined} {...register('confirm')} />
            </Field>
          </div>
        </Panel>

        <Panel title="Access">
          <div className="space-y-4">
            <Field label="Admin role" htmlFor="new-role" required error={formState.errors.role?.message} hint={<RoleSummary name={role} />}>
              <Controller
                control={control}
                name="role"
                render={({ field }) => (
                  <RoleSelect id="new-role" value={field.value} onChange={field.onChange} verb="add" invalid={Boolean(formState.errors.role)} />
                )}
              />
            </Field>
            <Controller
              control={control}
              name="is_active"
              render={({ field }) => (
                <SwitchRow
                  id="new-active"
                  label="Active"
                  hint={field.value ? 'The account can sign in as soon as it is created.' : 'The account exists but can’t sign in until you activate it.'}
                  checked={field.value}
                  onCheckedChange={field.onChange}
                />
              )}
            />
          </div>
        </Panel>

        <InlineError error={create.error} />
        <div className="flex justify-end gap-2">
          <Button variant="ghost" onClick={() => router.push('/users')}>
            Cancel
          </Button>
          <Button type="submit" variant="primary" loading={create.isPending}>
            {active ? 'Create user' : 'Create inactive user'}
          </Button>
        </div>
      </form>
    </div>
  )
}
