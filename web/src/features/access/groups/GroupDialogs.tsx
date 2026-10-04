'use client'

import React, { useEffect, useMemo, useState } from 'react'
import Link from 'next/link'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import { z } from 'zod'
import { useWs, useWsMutation } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { formatDate } from '@/lib/format'
import { Button } from '@/components/ui/Button'
import { confirm } from '@/components/ui/Confirm'
import { Dialog, DialogContent, SheetContent } from '@/components/ui/Dialog'
import { Field, Input, Select, Textarea } from '@/components/ui/Field'
import { IconButton } from '@/components/ui/IconButton'
import { InlineError } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { XmarkIcon } from '@/components/ui/icons'
import { Avatar } from '@/components/shell/UserMenu'
import type { GroupRecord } from '@/features/access/types'
import { userHref } from '@/features/access/shared'
import { GROUP_COMMANDS } from '@/features/access/groups/groupActions'

const schema = z.object({
  name: z.string().trim().min(3, 'At least 3 characters').max(50, 'At most 50 characters'),
  description: z.string().max(500, 'At most 500 characters'),
  gid: z
    .string()
    .trim()
    .refine(v => !v || (/^\d+$/.test(v) && Number(v) > 0 && Number(v) < 2 ** 32), 'A positive whole number'),
})
type Form = z.infer<typeof schema>

// Loaded with next/dynamic and mounted only while open (Radix Dialog stays out of the page's first load).

// Create (group null) or edit a group's name, description and Linux GID.
export const GroupFormDialog = ({ group, open, onOpenChange }: { group: GroupRecord | null; open: boolean; onOpenChange: (open: boolean) => void }) => {
  const editing = Boolean(group)
  const defaults = useMemo<Form>(
    () => ({ name: group?.name ?? '', description: group?.description ?? '', gid: group?.gid !== undefined ? String(group.gid) : '' }),
    [group],
  )
  const form = useForm<Form>({ resolver: zodResolver(schema), defaultValues: defaults })
  const { reset } = form

  const create = useWsMutation('group.add', {
    toPayload: (v: Form) => ({
      name: v.name.trim(),
      ...(v.description.trim() ? { description: v.description.trim() } : {}),
      ...(v.gid ? { linux_gid: Number(v.gid) } : {}),
    }),
    invalidates: [...GROUP_COMMANDS],
    onSuccess: data => {
      notify.success(`Created group ${data.group.name}`)
      onOpenChange(false)
    },
  })
  const update = useWsMutation('group.update', {
    // A patch with only what changed.
    toPayload: (v: Form) => ({
      id: group!.id,
      ...(v.name.trim() !== group!.name ? { name: v.name.trim() } : {}),
      ...(v.description.trim() !== (group!.description ?? '') ? { description: v.description.trim() } : {}),
      ...(v.gid && Number(v.gid) !== group!.gid ? { linux_gid: Number(v.gid) } : {}),
    }),
    invalidates: [...GROUP_COMMANDS],
    onSuccess: data => {
      notify.success(`Saved group ${data.group.name}`)
      onOpenChange(false)
    },
  })
  const mutation = editing ? update : create
  const { reset: resetCreate } = create
  const { reset: resetUpdate } = update

  useEffect(() => {
    if (open) {
      reset(defaults)
      resetCreate()
      resetUpdate()
    }
  }, [open, defaults, reset, resetCreate, resetUpdate])

  const errors = form.formState.errors
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent
        title={editing ? `Edit ${group!.name}` : 'New group'}
        description={editing ? undefined : 'Groups collect accounts so a vault role can be granted to all of them at once.'}
        footer={
          <>
            <Button variant="ghost" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button
              type="submit"
              form="group-form"
              variant="primary"
              loading={mutation.isPending}
              disabled={editing && !form.formState.isDirty}>
              {editing ? 'Save group' : 'Create group'}
            </Button>
          </>
        }>
        <form
          id="group-form"
          noValidate
          className="space-y-4"
          onSubmit={form.handleSubmit(values => (editing ? update.mutate(values) : create.mutate(values)))}>
          <Field label="Name" htmlFor="group-name" required error={errors.name?.message}>
            <Input id="group-name" autoFocus autoComplete="off" aria-invalid={Boolean(errors.name) || undefined} {...form.register('name')} />
          </Field>
          <Field label="Description" htmlFor="group-description" error={errors.description?.message}>
            <Textarea id="group-description" rows={2} {...form.register('description')} />
          </Field>
          <Field
            label="Linux GID"
            htmlFor="group-gid"
            error={errors.gid?.message}
            hint={
              editing && group?.gid !== undefined
                ? 'Maps the group to a Linux group for FUSE access. It can be changed but not removed.'
                : 'Optional. Maps the group to a Linux group for FUSE access.'
            }>
            <Input id="group-gid" inputMode="numeric" className="tabular" autoComplete="off" aria-invalid={Boolean(errors.gid) || undefined} {...form.register('gid')} />
          </Field>
          <InlineError error={mutation.error} />
        </form>
      </DialogContent>
    </Dialog>
  )
}

export const MembersSheet = ({ group, open, onOpenChange }: { group: GroupRecord | null; open: boolean; onOpenChange: (open: boolean) => void }) => {
  const canAdd = useCan({ permission: 'admin.identities.groups.add-member' })
  const canRemove = useCan({ permission: 'admin.identities.groups.remove-member' })
  const canViewUsers = useCan({ anyOf: ['admin.identities.users.view', 'admin.identities.admins.view'] })
  const users = useWs('auth.users.list', null, { enabled: open && canAdd && canViewUsers })
  const [pick, setPick] = useState('')

  const add = useWsMutation('group.member.add', {
    invalidates: [...GROUP_COMMANDS],
    onSuccess: (_data, input) => {
      setPick('')
      notify.success(`Added ${users.data?.users.find(u => u.id === input.user_id)?.name ?? 'member'}`)
    },
  })
  const remove = useWsMutation('group.member.remove', { invalidates: [...GROUP_COMMANDS] })

  const members = useMemo(() => [...(group?.members ?? [])].sort((a, b) => a.user.name.localeCompare(b.user.name)), [group])
  const candidates = useMemo(
    () => (users.data?.users ?? []).filter(u => !u.system_only && !members.some(m => m.user.id === u.id)).sort((a, b) => a.name.localeCompare(b.name)),
    [users.data, members],
  )

  const removeMember = async (userId: number, name: string) => {
    if (!group) return
    const ok = await confirm({
      title: `Remove ${name} from ${group.name}?`,
      description: `${name} loses every vault role granted through ${group.name}.`,
      confirmLabel: 'Remove',
    })
    if (!ok) return
    remove.mutate(
      { group_id: group.id, user_id: userId },
      { onSuccess: () => notify.success(`Removed ${name}`), onError: error => notify.error(error, 'Could not remove the member') },
    )
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      {group ? (
        <SheetContent title={group.name} description={group.description || 'Members of this group'} width="max-w-lg">
          <div className="space-y-4">
            {canAdd && canViewUsers ? (
              <form
                className="flex gap-2"
                onSubmit={event => {
                  event.preventDefault()
                  if (pick) add.mutate({ group_id: group.id, user_id: Number(pick) })
                }}>
                <div className="min-w-0 flex-1">
                  <Select aria-label="Account to add" value={pick} onChange={event => setPick(event.target.value)} disabled={!candidates.length}>
                    <option value="">{users.isPending ? 'Loading accounts…' : candidates.length ? 'Add an account…' : 'Everyone is already a member'}</option>
                    {candidates.map(u => (
                      <option key={u.id} value={String(u.id)}>
                        {u.name}
                        {u.is_active ? '' : ' (inactive)'}
                      </option>
                    ))}
                  </Select>
                </div>
                <Button type="submit" variant="secondary" disabled={!pick} loading={add.isPending}>
                  Add
                </Button>
              </form>
            ) : null}
            <InlineError error={add.error} />

            <div>
              <h3 className="mb-2 text-xs font-medium tracking-wide text-fg-subtle uppercase">
                Members <span className="tabular">({members.length})</span>
              </h3>
              {members.length ? (
                <ul className="divide-y divide-line/60 rounded-card border border-line text-sm">
                  {members.map(member => (
                    <li key={member.user.id} className="flex items-center gap-3 px-3 py-2">
                      <Avatar name={member.user.name} />
                      <div className="min-w-0 flex-1">
                        {canViewUsers ? (
                          <Link href={userHref(member.user.name)} className="block truncate font-medium text-fg hover:text-accent-text">
                            {member.user.name}
                          </Link>
                        ) : (
                          <span className="block truncate font-medium text-fg">{member.user.name}</span>
                        )}
                        <span className="text-xs text-fg-subtle">
                          {member.user.is_active ? '' : 'Inactive · '}Joined <span className="tabular">{formatDate(member.joined_at)}</span>
                        </span>
                      </div>
                      {canRemove ? (
                        <IconButton
                          size="icon-sm"
                          icon={XmarkIcon}
                          label={`Remove ${member.user.name}`}
                          onClick={() => void removeMember(member.user.id, member.user.name)}
                          disabled={remove.isPending}
                        />
                      ) : null}
                    </li>
                  ))}
                </ul>
              ) : (
                <p className="rounded-card border border-dashed border-line px-4 py-6 text-center text-sm text-fg-subtle">No members yet.</p>
              )}
            </div>
          </div>
        </SheetContent>
      ) : null}
    </Dialog>
  )
}
