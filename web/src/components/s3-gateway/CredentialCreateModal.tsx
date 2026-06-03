'use client'

import { useState } from 'react'
import PlusIcon from '@/fa-duotone/plus.svg'
import { S3GatewayCredential, S3GatewayCredentialCreatePayload, S3GatewayCredentialScopeMode } from '@/models/s3Gateway'
import type { User } from '@/models/user'
import {
  buttonClass,
  fieldClass,
  primaryButtonClass,
  scopeLabel,
  scopeModes,
} from './shared'

export function CredentialCreateModal({
  open,
  saving,
  users,
  currentUser,
  canAssignPrincipal,
  onClose,
  onCreate,
  onCreated,
}: {
  open: boolean
  saving: boolean
  users: User[]
  currentUser: User | null
  canAssignPrincipal: boolean
  onClose: () => void
  onCreate: (payload: S3GatewayCredentialCreatePayload) => Promise<{ credential: S3GatewayCredential; secret_access_key: string }>
  onCreated: (credentialId: number) => void
}) {
  const [newName, setNewName] = useState('')
  const [newPrincipalId, setNewPrincipalId] = useState('')
  const [newScopeMode, setNewScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [newDescription, setNewDescription] = useState('')
  const [newExpiresDays, setNewExpiresDays] = useState('')
  const [enforceLocalBudget, setEnforceLocalBudget] = useState(false)

  if (!open) return null

  const submitCreate = async () => {
    const expires = Number(newExpiresDays)
    const expires_at = Number.isFinite(expires) && expires > 0
      ? Math.floor(Date.now() / 1000) + Math.floor(expires * 86400)
      : null
    const principal = Number(newPrincipalId)
    const result = await onCreate({
      name: newName,
      ...(canAssignPrincipal && Number.isFinite(principal) && principal > 0 ? { principal_user_id: principal } : {}),
      scope_mode: newScopeMode,
      description: newDescription.trim() || null,
      expires_at,
      enforce_budget_for_local_requests: enforceLocalBudget,
    })
    onCreated(result.credential.id)
    onClose()
    setNewName('')
    setNewPrincipalId('')
    setNewDescription('')
    setNewExpiresDays('')
    setEnforceLocalBudget(false)
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4" data-testid="s3-gateway-create-credential-modal">
      <div className="max-h-[90vh] w-full max-w-3xl overflow-y-auto rounded border border-white/10 bg-zinc-950 p-5 text-white shadow-2xl">
        <div className="mb-4 flex items-center justify-between gap-3">
          <h2 className="text-lg font-semibold text-cyan-100">Create credential</h2>
          <button className={buttonClass} type="button" onClick={onClose}>Close</button>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <label className="flex flex-col gap-1 text-xs text-white/60">
            Name
            <input className={fieldClass} data-testid="s3-gateway-credential-name-input" value={newName} onChange={event => setNewName(event.target.value)} />
          </label>
          {canAssignPrincipal ? (
            <label className="flex flex-col gap-1 text-xs text-white/60">
              Principal
              <select className={fieldClass} data-testid="s3-gateway-credential-principal-select" value={newPrincipalId} onChange={event => setNewPrincipalId(event.target.value)}>
                <option value="">You{currentUser ? ` (${currentUser.name})` : ''}</option>
                {users.map(user => (
                  <option key={user.id} value={user.id}>
                    {user.name} ({user.email})
                  </option>
                ))}
              </select>
            </label>
          ) : (
            <div className="flex flex-col gap-1 text-xs text-white/60">
              Principal
              <div className={`${fieldClass} flex items-center text-white/75`} data-testid="s3-gateway-credential-principal-fixed">
                You{currentUser ? ` (${currentUser.name})` : ''}
              </div>
            </div>
          )}
          <label className="flex flex-col gap-1 text-xs text-white/60">
            Scope
            <select className={fieldClass} data-testid="s3-gateway-credential-scope-select" value={newScopeMode} onChange={event => setNewScopeMode(event.target.value as S3GatewayCredentialScopeMode)}>
              {scopeModes.map(mode => <option key={mode} value={mode}>{scopeLabel(mode)}</option>)}
            </select>
          </label>
          <label className="flex flex-col gap-1 text-xs text-white/60">
            Expires in days
            <input className={fieldClass} inputMode="decimal" value={newExpiresDays} onChange={event => setNewExpiresDays(event.target.value)} />
          </label>
          <label className="flex flex-col gap-1 text-xs text-white/60 md:col-span-2">
            Description
            <input className={fieldClass} value={newDescription} onChange={event => setNewDescription(event.target.value)} />
          </label>
          <label className="flex items-start gap-3 rounded border border-white/10 bg-white/[0.03] p-3 text-sm text-white/75 md:col-span-2">
            <input
              className="mt-1 h-4 w-4 accent-cyan-400"
              data-testid="s3-gateway-create-enforce-local-budget"
              type="checkbox"
              checked={enforceLocalBudget}
              onChange={event => setEnforceLocalBudget(event.target.checked)}
            />
            <span>
              <span className="block font-medium text-white">Count local/cache hits against gateway request budgets</span>
              <span className="mt-1 block text-xs leading-5 text-white/55">
                Default off. When off, cache/local hits do not consume upstream-style request budgets. When on, local/cache hits are treated as gateway usage for this key even when no upstream S3/R2 call occurs.
              </span>
            </span>
          </label>
        </div>

        {newScopeMode === 'vault_allowlist' && (
          <div className="mt-4 rounded border border-amber-300/20 bg-amber-400/10 p-3 text-sm text-amber-50">
            Create the credential, then assign at least one vault role in the credential role editor.
          </div>
        )}

        <div className="mt-5 flex justify-end gap-2">
          <button className={buttonClass} type="button" onClick={onClose}>Cancel</button>
          <button className={primaryButtonClass} data-testid="s3-gateway-submit-create-credential" type="button" disabled={!newName.trim() || saving} onClick={() => void submitCreate()}>
            <PlusIcon className="h-4 w-4" />
            Create
          </button>
        </div>
      </div>
    </div>
  )
}
