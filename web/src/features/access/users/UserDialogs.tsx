'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { api } from '@/lib/session'
import { invalidate, useWs, useWsMutation } from '@/lib/query'
import { isWsError } from '@/lib/ws/errors'
import { Button } from '@/components/ui/Button'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Field, Input, Select } from '@/components/ui/Field'
import { InlineError, Skeleton } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import type { UserRecord } from '@/features/access/types'
import { passwordPair } from '@/features/access/users/schemas'

const resetSchema = z.object({ password: z.string(), confirm: z.string() }).superRefine(passwordPair('password', 'confirm'))
type ResetForm = z.infer<typeof resetSchema>

export const ResetPasswordDialog = ({ user, open, onOpenChange }: { user: UserRecord; open: boolean; onOpenChange: (open: boolean) => void }) => {
  const form = useForm<ResetForm>({ resolver: zodResolver(resetSchema), defaultValues: { password: '', confirm: '' } })
  const reset = useWsMutation('auth.user.change_password', {
    toPayload: (values: ResetForm) => ({ id: user.id, new_password: values.password }),
    invalidates: ['auth.user.get.byName', 'auth.users.list'],
    onSuccess: () => {
      notify.success(`Password reset for ${user.name}`, 'Their sessions were ended.')
      onOpenChange(false)
    },
  })
  const { reset: resetForm } = form
  const { reset: resetMutation } = reset
  useEffect(() => {
    if (!open) {
      resetForm()
      resetMutation()
    }
  }, [open, resetForm, resetMutation])

  const errors = form.formState.errors
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent
        size="sm"
        title={`Reset ${user.name}’s password`}
        description="Set a new password and give it to them over a trusted channel. Every session they have open ends."
        footer={
          <>
            <Button variant="ghost" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button variant="primary" type="submit" form="reset-password-form" loading={reset.isPending}>
              Reset password
            </Button>
          </>
        }>
        <form id="reset-password-form" noValidate onSubmit={form.handleSubmit(values => reset.mutate(values))} className="space-y-4">
          <Field label="New password" htmlFor="reset-new" required error={errors.password?.message} hint="Weak or breached passwords are refused.">
            <Input id="reset-new" type="password" autoComplete="new-password" autoFocus aria-invalid={Boolean(errors.password) || undefined} {...form.register('password')} />
          </Field>
          <Field label="Confirm password" htmlFor="reset-confirm" required error={errors.confirm?.message}>
            <Input id="reset-confirm" type="password" autoComplete="new-password" aria-invalid={Boolean(errors.confirm) || undefined} {...form.register('confirm')} />
          </Field>
          <InlineError error={reset.error} />
        </form>
      </DialogContent>
    </Dialog>
  )
}

type Question = { state: 'asking' } | { state: 'asked'; text: string } | { state: 'error'; error: unknown }

// Server-driven delete: an unconfirmed auth.user.delete is refused with data.code 'user_delete' and the exact question
// to ask (the same one `vh user delete` asks, listing the vaults that go with the account). The answer is resent with
// confirm, plus transfer_to to keep the vaults.
export const DeleteUserDialog = ({
  user,
  open,
  onOpenChange,
  onDeleted,
}: {
  user: UserRecord
  open: boolean
  onOpenChange: (open: boolean) => void
  onDeleted: () => void
}) => {
  const [transferTo, setTransferTo] = useState('')
  const [question, setQuestion] = useState<Question>({ state: 'asking' })
  const users = useWs('auth.users.list', null, { enabled: open })
  const heirs = useMemo(
    () => (users.data?.users ?? []).filter(u => u.id !== user.id && u.is_active && !u.system_only).sort((a, b) => a.name.localeCompare(b.name)),
    [users.data, user.id],
  )

  useEffect(() => {
    if (!open) return
    let current = true
    // The question names the heir when one is chosen, so it is asked again whenever the choice changes.
    api
      .send('auth.user.delete', { id: user.id, ...(transferTo ? { transfer_to: Number(transferTo) } : {}) })
      .then(async () => {
        // A daemon without the confirmation step deleted the account outright.
        if (!current) return
        await invalidate('auth.users.list')
        notify.success(`Deleted ${user.name}`)
        onDeleted()
      })
      .catch(error => {
        if (!current) return
        if (isWsError(error, 'needs_confirmation') && error.code === 'user_delete') setQuestion({ state: 'asked', text: error.message })
        else setQuestion({ state: 'error', error })
      })
    return () => {
      current = false
    }
  }, [open, user.id, user.name, transferTo, onDeleted])

  useEffect(() => {
    if (!open) {
      setTransferTo('')
      setQuestion({ state: 'asking' })
    }
  }, [open])

  const remove = useWsMutation<'auth.user.delete', void>('auth.user.delete', {
    toPayload: () => ({ id: user.id, confirm: true, transfer_to: transferTo ? Number(transferTo) : null }),
    invalidates: ['auth.users.list', 'storage.vault.list', 'groups.list'],
    onSuccess: () => {
      notify.success(`Deleted ${user.name}`, transferTo ? `Their vaults now belong to ${heirs.find(h => String(h.id) === transferTo)?.name ?? 'the new owner'}.` : undefined)
      onDeleted()
    },
  })

  const [lead, ...rest] = question.state === 'asked' ? question.text.split('\n') : []
  return (
    <Dialog open={open} onOpenChange={next => !remove.isPending && onOpenChange(next)}>
      <DialogContent
        size="md"
        title={`Delete ${user.name}?`}
        footer={
          <>
            <Button variant="ghost" onClick={() => onOpenChange(false)} disabled={remove.isPending}>
              Cancel
            </Button>
            <Button variant="danger-solid" loading={remove.isPending} disabled={question.state !== 'asked'} onClick={() => remove.mutate()}>
              Delete user
            </Button>
          </>
        }>
        <div className="space-y-4 text-sm">
          {question.state === 'asking' ? (
            <div className="space-y-2">
              <Skeleton className="w-full" />
              <Skeleton className="w-2/3" />
            </div>
          ) : question.state === 'error' ? (
            <InlineError error={question.error} />
          ) : (
            <div className="space-y-2 text-fg-muted">
              <p>{lead}</p>
              {rest.length ? (
                <div className="rounded-control border border-line bg-surface-1 px-3 py-2.5 whitespace-pre-line text-fg">{rest.join('\n')}</div>
              ) : null}
            </div>
          )}
          <Field
            label="Their vaults"
            htmlFor="delete-transfer"
            hint={transferTo ? 'Ownership moves to this account before the user is deleted.' : 'Every vault this account owns is destroyed with it.'}>
            <Select id="delete-transfer" value={transferTo} onChange={event => setTransferTo(event.target.value)} disabled={remove.isPending}>
              <option value="">Destroy them</option>
              {heirs.map(heir => (
                <option key={heir.id} value={String(heir.id)}>
                  Transfer to {heir.name}
                </option>
              ))}
            </Select>
          </Field>
          <InlineError error={remove.error} />
        </div>
      </DialogContent>
    </Dialog>
  )
}
