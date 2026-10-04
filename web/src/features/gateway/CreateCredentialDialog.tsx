'use client'

import React, { useState } from 'react'
import { api, useSession } from '@/lib/session'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Field, Input, Select, Textarea } from '@/components/ui/Field'
import { Checkbox } from '@/components/ui/Choice'
import { CheckRow } from '@/features/gateway/CheckRow'
import { InlineError } from '@/components/ui/State'
import { KeyIcon } from '@/components/ui/icons'
import { CopyField } from '@/features/gateway/CopyField'
import { cn } from '@/util/cn'
import { SCOPE_MODES, toCredential, type GatewayCredential, type ScopeMode } from '@/features/gateway/model'
import {
  refreshCredentials,
  useGatewayPerms,
  useUserOptions,
  useVaultOptions,
  useVaultRoleOptions,
} from '@/features/gateway/queries'

// The one-time secret: the server never returns it again.
const Created = ({
  credential,
  secret,
  onDone,
}: {
  credential: GatewayCredential
  secret: string
  onDone: () => void
}) => (
  <DialogContent
    size="md"
    title="Key created — copy the secret now"
    description="For security the secret access key is shown only once. If it’s lost, revoke the key and create a new one."
    hideClose
    footer={
      <Button variant="primary" onClick={onDone} data-testid="s3-gateway-hide-secret">
        I’ve saved it
      </Button>
    }>
    <div
      className="rounded-card border-accent-line bg-accent-soft space-y-4 border p-4"
      data-testid="s3-gateway-secret-panel">
      <CopyField label="Access key ID" value={credential.access_key} />
      <CopyField label="Secret access key" value={secret} secret />
    </div>
    <p className="text-fg-subtle mt-3 text-xs">
      “{credential.name}” acts as {credential.principal_user?.name ?? 'its principal'}. Configure clients under the
      Client setup tab.
    </p>
  </DialogContent>
)

const CreateForm = ({
  onCreated,
  onCancel,
}: {
  onCreated: (c: GatewayCredential, secret: string) => void
  onCancel: () => void
}) => {
  const me = useSession(s => s.user)
  const can = useGatewayPerms()
  const users = useUserOptions(can.assignPrincipal)
  const roles = useVaultRoleOptions()
  const vaults = useVaultOptions()
  const [name, setName] = useState('')
  const [principal, setPrincipal] = useState<number | null>(null)
  const [scope, setScope] = useState<ScopeMode>('user_access')
  const [roleId, setRoleId] = useState<number | null>(null)
  const [selected, setSelected] = useState<number[]>([])
  const [expiresDays, setExpiresDays] = useState('')
  const [description, setDescription] = useState('')
  const [localBudget, setLocalBudget] = useState(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)

  const days = expiresDays.trim() ? Number(expiresDays) : null
  const problems = {
    name: name.trim() ? null : 'Give the key a name',
    role: scope !== 'user_access' && !roleId ? 'Pick the vault role this key gets' : null,
    vaults: scope === 'vault_allowlist' && selected.length === 0 ? 'Select at least one vault' : null,
    expires: days !== null && (!Number.isFinite(days) || days <= 0) ? 'Use a number of days greater than zero' : null,
  }
  const [touched, setTouched] = useState(false)
  const invalid = Object.values(problems).some(Boolean)

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setTouched(true)
    if (invalid) return
    setBusy(true)
    setError(null)
    try {
      const res = await api.send('s3.gateway.credentials.create', {
        name: name.trim(),
        ...(can.assignPrincipal && principal ? { principal_user_id: principal } : {}),
        scope_mode: scope,
        description: description.trim() || null,
        expires_at: days ? Math.floor(Date.now() / 1000) + Math.round(days * 86_400) : null,
        ...(scope !== 'user_access' && roleId ? { default_vault_role_id: roleId } : {}),
        ...(scope === 'vault_allowlist' ? { selected_vault_ids: selected } : {}),
        enforce_budget_for_local_requests: localBudget,
      })
      await refreshCredentials()
      onCreated(toCredential(res.credential), res.secret_access_key)
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const show = (key: keyof typeof problems) => (touched ? (problems[key] ?? undefined) : undefined)

  return (
    <DialogContent
      size="lg"
      data-testid="s3-gateway-create-credential-modal"
      title="Create gateway key"
      description="An access key and secret an S3 client uses to reach bound buckets."
      footer={
        <>
          <Button variant="ghost" onClick={onCancel}>
            Cancel
          </Button>
          <Button
            type="submit"
            form="gw-create"
            variant="primary"
            loading={busy}
            data-testid="s3-gateway-submit-create-credential">
            <KeyIcon aria-hidden />
            Create key
          </Button>
        </>
      }>
      <form id="gw-create" onSubmit={submit} className="space-y-5" noValidate>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label="Name" htmlFor="gw-name" required error={show('name')}>
            <Input
              id="gw-name"
              data-testid="s3-gateway-credential-name-input"
              value={name}
              onChange={e => setName(e.target.value)}
              placeholder="e.g. nightly-backup"
              autoComplete="off"
            />
          </Field>
          <Field
            label="Acts as"
            htmlFor="gw-principal"
            hint={can.assignPrincipal ? 'The user whose access the key uses.' : undefined}>
            {can.assignPrincipal ?
              <Select
                id="gw-principal"
                data-testid="s3-gateway-credential-principal-select"
                value={principal ?? ''}
                onChange={e => setPrincipal(e.target.value ? Number(e.target.value) : null)}>
                <option value="">You{me ? ` (${me.name})` : ''}</option>
                {users
                  .filter(u => u.id !== me?.id)
                  .map(u => (
                    <option key={u.id} value={u.id}>
                      {u.name}
                    </option>
                  ))}
              </Select>
            : <Input id="gw-principal" value={`You${me ? ` (${me.name})` : ''}`} disabled />}
          </Field>
        </div>

        <fieldset>
          <legend className="text-fg-muted mb-2 text-[13px] font-medium">Access</legend>
          <div className="grid gap-2 sm:grid-cols-3" role="radiogroup" aria-label="Access">
            {SCOPE_MODES.map(m => (
              <button
                key={m.value}
                type="button"
                role="radio"
                data-testid={`s3-gateway-credential-scope-${m.value}`}
                aria-checked={scope === m.value}
                onClick={() => setScope(m.value)}
                className={cn(
                  'rounded-card border-line bg-surface-1 hover:border-line-strong border p-3 text-left transition-colors',
                  scope === m.value && 'border-accent-line bg-accent-soft',
                )}>
                <div className="text-fg text-sm font-medium">{m.label}</div>
                <div className="text-fg-subtle mt-1 text-xs leading-snug">{m.hint}</div>
              </button>
            ))}
          </div>
        </fieldset>

        {scope !== 'user_access' ?
          <Field
            label="Default vault role"
            htmlFor="gw-role"
            required
            error={show('role')}
            hint="What the key may do in each vault it reaches.">
            <Select
              id="gw-role"
              data-testid="s3-gateway-create-default-role-select"
              value={roleId ?? ''}
              onChange={e => setRoleId(e.target.value ? Number(e.target.value) : null)}>
              <option value="">Choose a role…</option>
              {roles.map(r => (
                <option key={r.id} value={r.id}>
                  {r.name}
                </option>
              ))}
            </Select>
          </Field>
        : null}

        {scope === 'vault_allowlist' ?
          <Field label="Vaults" error={show('vaults')}>
            <div
              data-testid="s3-gateway-create-selected-vaults"
              className="rounded-control border-line bg-surface-1 max-h-44 space-y-1 overflow-y-auto border p-2">
              {vaults.list.length === 0 ?
                <p className="text-fg-subtle px-1 py-2 text-sm">No vaults you can see.</p>
              : vaults.list.map(v => (
                  <label
                    key={v.id}
                    className="text-fg hover:bg-surface-2 flex cursor-pointer items-center gap-2.5 rounded-md px-2 py-1.5 text-sm">
                    <Checkbox
                      checked={selected.includes(v.id)}
                      onCheckedChange={on => setSelected(cur => (on ? [...cur, v.id] : cur.filter(id => id !== v.id)))}
                    />
                    {v.name}
                    <span className="text-fg-faint ml-auto text-xs">{v.type === 's3' ? 'S3' : 'Local'}</span>
                  </label>
                ))
              }
            </div>
          </Field>
        : null}

        <div className="grid gap-4 sm:grid-cols-[12rem_1fr]">
          <Field label="Expires after" htmlFor="gw-expires" error={show('expires')} hint="Empty: never expires.">
            <div className="relative">
              <Input
                id="gw-expires"
                type="number"
                min={1}
                inputMode="numeric"
                value={expiresDays}
                onChange={e => setExpiresDays(e.target.value)}
                className="tabular pr-12"
              />
              <span className="text-fg-subtle pointer-events-none absolute top-1/2 right-3 -translate-y-1/2 text-xs">
                days
              </span>
            </div>
          </Field>
          <Field label="Description" htmlFor="gw-desc">
            <Textarea
              id="gw-desc"
              rows={1}
              className="min-h-9"
              value={description}
              onChange={e => setDescription(e.target.value)}
            />
          </Field>
        </div>

        <CheckRow
          id="gw-local-budget"
          testId="s3-gateway-create-enforce-local-budget"
          label="Count cache and local hits against this key’s budget"
          hint="Off by default: requests served without an upstream S3 call don’t use up budgets. On: every request is gateway usage for this key."
          checked={localBudget}
          onCheckedChange={setLocalBudget}
        />
        <InlineError error={error} />
      </form>
    </DialogContent>
  )
}

export default function CreateCredentialDialog({
  onClose,
  onCreated,
}: {
  onClose: () => void
  onCreated: (c: GatewayCredential) => void
}) {
  const [created, setCreated] = useState<{ credential: GatewayCredential; secret: string } | null>(null)
  return (
    <Dialog open onOpenChange={open => !open && !created && onClose()}>
      {created ?
        <Created
          credential={created.credential}
          secret={created.secret}
          onDone={() => {
            onCreated(created.credential)
            onClose()
          }}
        />
      : <CreateForm onCreated={(credential, secret) => setCreated({ credential, secret })} onCancel={onClose} />}
    </Dialog>
  )
}
