'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { useWs, invalidate } from '@/lib/query'
import { api } from '@/lib/session'
import { cn } from '@/util/cn'
import type { ShareAccessMode } from '@/models/linkShare'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Field, Input, Select } from '@/components/ui/Field'
import { Segmented, Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/Tabs'
import { InlineError } from '@/components/ui/State'
import { DownloadIcon, EyeIcon, UploadIcon, XmarkIcon, PlusIcon } from '@/components/ui/icons'
import type { Entry } from '@/features/files/entries'
import { PRESETS, presetOperations, publicUrl, type SharePreset } from '@/features/shares/shareMeta'
import { OneTimeUrl, ShareLinkList, SharingNotice } from '@/features/shares/ShareLinks'
import { useSharingPolicy } from '@/features/shares/policy'

const PRESET_ICONS = { access: EyeIcon, download: DownloadIcon, upload: UploadIcon }

const ACCESS_OPTIONS: { value: ShareAccessMode; label: string }[] = [
  { value: 'public', label: 'Anyone with the link' },
  { value: 'email_validated', label: 'Verified email only' },
]

export const ShareDialog = ({ vaultId, target, onClose }: { vaultId: number; target: Entry | null; onClose: () => void }) => {
  const isDir = target?.kind === 'dir'
  const [preset, setPreset] = useState<SharePreset>('access')
  const [access, setAccess] = useState<ShareAccessMode>('public')
  const [label, setLabel] = useState('')
  const [expires, setExpires] = useState('')
  const [roleId, setRoleId] = useState<number | null>(null)
  const [recipients, setRecipients] = useState<{ email: string; roleId: number }[]>([])
  const [recipientEmail, setRecipientEmail] = useState('')
  const [url, setUrl] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)
  const [tab, setTab] = useState('new')

  const roles = useWs('roles.vault.list', null, { enabled: Boolean(target), staleTime: 5 * 60_000 })
  const templates = useMemo(() => roles.data?.roles ?? [], [roles.data])
  const presetRole = (p: SharePreset) => templates.find(r => r.name === PRESETS[p].role) ?? null
  // Only the kinds of link the operator allows (sharing.*); the daemon refuses the rest anyway.
  const policy = useSharingPolicy()
  const accessOptions = ACCESS_OPTIONS.filter(o => policy.modes.includes(o.value))

  // Keep the choice on an allowed kind (the form resets to 'public', and the policy may load after it opens).
  useEffect(() => {
    if (!policy.modes.includes(access) && policy.modes.length) setAccess(policy.modes[0])
  }, [access, policy.modes])

  useEffect(() => {
    if (!target) return
    setPreset(target.kind === 'dir' ? 'access' : 'download')
    setAccess('public')
    setLabel(target.name || 'Shared files')
    setExpires('')
    setRoleId(null)
    setRecipients([])
    setUrl(null)
    setError(null)
    setTab('new')
  }, [target])

  const effectiveRole = roleId ?? presetRole(preset)?.id ?? templates.find(r => r.name === 'implicit_deny')?.id ?? null

  const create = async (event: React.FormEvent) => {
    event.preventDefault()
    if (!target) return
    if (!effectiveRole) return setError(new Error('No share role template is available. Check Roles → Vault.'))
    setBusy(true)
    setError(null)
    try {
      const res = await api.send('share.link.create', {
        vault_id: vaultId,
        root_entry_id: target.id,
        root_path: target.path,
        target_type: isDir ? 'directory' : 'file',
        link_type: PRESETS[preset].linkType,
        access_mode: access,
        allowed_ops: presetOperations(preset, isDir),
        name: label,
        public_label: label,
        expires_at: expires ? new Date(expires).toISOString() : null,
        duplicate_policy: 'reject',
        public_role_assignment: { vault_role_id: effectiveRole },
        recipient_role_assignments:
          access === 'email_validated' ? recipients.map(r => ({ email: r.email, role_assignment: { vault_role_id: r.roleId } })) : [],
      })
      setUrl(publicUrl(res.public_url_path))
      await invalidate('share.link.list')
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const addRecipient = () => {
    const email = recipientEmail.trim()
    if (!email || !email.includes('@') || !effectiveRole) return
    setRecipients(list => [...list.filter(r => r.email !== email), { email, roleId: effectiveRole }])
    setRecipientEmail('')
  }

  return (
    <Dialog open={Boolean(target)} onOpenChange={open => !open && onClose()}>
      {target ? (
        <DialogContent size="lg" title={`Share “${target.name || 'this folder'}”`} description={target.path}>
          <Tabs value={tab} onValueChange={setTab}>
            <TabsList>
              <TabsTrigger value="new">New link</TabsTrigger>
              <TabsTrigger value="existing">Existing links</TabsTrigger>
            </TabsList>
            <TabsContent value="new">
              {!url && policy.loaded && !policy.modes.length ? (
                <div className="space-y-4">
                  <SharingNotice />
                  <div className="flex justify-end">
                    <Button variant="ghost" onClick={onClose}>
                      Close
                    </Button>
                  </div>
                </div>
              ) : url ? (
                <div className="space-y-4">
                  <OneTimeUrl url={url} />
                  <div className="flex justify-end gap-2">
                    <Button variant="ghost" onClick={() => setUrl(null)}>
                      Create another
                    </Button>
                    <Button variant="primary" onClick={onClose}>
                      Done
                    </Button>
                  </div>
                </div>
              ) : (
                <form onSubmit={create} className="space-y-5">
                  <fieldset>
                    <legend className="mb-2 text-[13px] font-medium text-fg-muted">What can people do?</legend>
                    <div className="grid gap-2 sm:grid-cols-3" role="radiogroup">
                      {(Object.keys(PRESETS) as SharePreset[]).map(p => {
                        const Icon = PRESET_ICONS[p]
                        const disabled = PRESETS[p].dirOnly && !isDir
                        const active = preset === p
                        return (
                          <button
                            key={p}
                            type="button"
                            role="radio"
                            aria-checked={active}
                            disabled={disabled}
                            onClick={() => {
                              setPreset(p)
                              setRoleId(null)
                            }}
                            className={cn(
                              'flex flex-col items-start gap-1.5 rounded-card border border-line bg-surface-1 p-3 text-left transition-colors hover:border-line-strong disabled:cursor-not-allowed disabled:opacity-40',
                              active && 'border-accent-line bg-accent-soft hover:border-accent-line',
                            )}>
                            <Icon className={cn('size-4 text-fg-subtle', active && 'text-accent-text')} aria-hidden />
                            <span className="text-sm font-medium text-fg">{PRESETS[p].title}</span>
                            <span className="text-xs leading-snug text-fg-subtle">{disabled ? 'Folders only' : PRESETS[p].description}</span>
                          </button>
                        )
                      })}
                    </div>
                  </fieldset>

                  <div className="grid gap-4 sm:grid-cols-2">
                    <Field label="Label recipients see" htmlFor="share-label">
                      <Input id="share-label" value={label} onChange={e => setLabel(e.target.value)} required />
                    </Field>
                    <Field label="Expires" htmlFor="share-expires" hint="Leave empty for no expiry">
                      <Input id="share-expires" type="datetime-local" value={expires} onChange={e => setExpires(e.target.value)} />
                    </Field>
                  </div>

                  <div className="space-y-2">
                    <div className="text-[13px] font-medium text-fg-muted">Who can open it?</div>
                    <Segmented label="Who can open it" value={access} onChange={setAccess} options={accessOptions} />
                    {accessOptions.length < ACCESS_OPTIONS.length ? (
                      <p className="text-xs text-fg-subtle">Other kinds of link are turned off on this server.</p>
                    ) : null}
                    {access === 'email_validated' ? (
                      <div className="space-y-2 rounded-card border border-line bg-surface-1 p-3">
                        <p className="text-xs text-fg-subtle">Recipients confirm a code sent to their inbox before the link opens.</p>
                        <div className="flex gap-2">
                          <Input
                            type="email"
                            placeholder="person@example.com"
                            aria-label="Recipient email"
                            value={recipientEmail}
                            onChange={e => setRecipientEmail(e.target.value)}
                            onKeyDown={e => {
                              if (e.key === 'Enter') {
                                e.preventDefault()
                                addRecipient()
                              }
                            }}
                          />
                          <Button onClick={addRecipient}>
                            <PlusIcon aria-hidden /> Add
                          </Button>
                        </div>
                        {recipients.length ? (
                          <ul className="flex flex-wrap gap-1.5">
                            {recipients.map(r => (
                              <li key={r.email} className="inline-flex items-center gap-1.5 rounded-full border border-line bg-surface-2 py-0.5 pr-1 pl-2.5 text-xs text-fg">
                                {r.email}
                                <button
                                  type="button"
                                  aria-label={`Remove ${r.email}`}
                                  onClick={() => setRecipients(list => list.filter(x => x.email !== r.email))}
                                  className="rounded-full p-0.5 text-fg-subtle hover:bg-surface-3 hover:text-fg">
                                  <XmarkIcon className="size-3" aria-hidden />
                                </button>
                              </li>
                            ))}
                          </ul>
                        ) : null}
                      </div>
                    ) : null}
                  </div>

                  <details className="group rounded-card border border-line bg-surface-1 px-3 py-2">
                    <summary className="cursor-pointer text-[13px] text-fg-muted select-none">Advanced: role template</summary>
                    <div className="mt-3 pb-1">
                      <Field label="Vault role applied to recipients" htmlFor="share-role" hint="Presets pick the matching built-in template. Choose another to customise.">
                        <Select id="share-role" value={effectiveRole ?? ''} onChange={e => setRoleId(Number(e.target.value) || null)}>
                          {templates.map(role => (
                            <option key={role.id} value={role.id}>
                              {role.name.replace(/_/g, ' ')}
                            </option>
                          ))}
                        </Select>
                      </Field>
                    </div>
                  </details>

                  <InlineError error={error} />
                  <div className="flex justify-end gap-2">
                    <Button variant="ghost" onClick={onClose}>
                      Cancel
                    </Button>
                    <Button type="submit" variant="primary" loading={busy}>
                      Create link
                    </Button>
                  </div>
                </form>
              )}
            </TabsContent>
            <TabsContent value="existing">
              <ShareLinkList vaultId={vaultId} rootEntryId={target.id} emptyText="No links for this item yet." />
            </TabsContent>
          </Tabs>
        </DialogContent>
      ) : null}
    </Dialog>
  )
}
