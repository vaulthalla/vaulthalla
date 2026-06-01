'use client'

import { useState } from 'react'
import PlusIcon from '@/fa-duotone/plus.svg'
import { S3GatewayCredential, S3GatewayCredentialCreatePayload, S3GatewayCredentialScopeMode } from '@/models/s3Gateway'
import type { Vault } from '@/models/vaults'
import {
  buttonClass,
  fieldClass,
  PermissionCheckbox,
  permissionKeys,
  primaryButtonClass,
  scopeLabel,
  scopeModes,
} from './shared'

export function CredentialCreateModal({
  open,
  saving,
  vaults,
  onClose,
  onCreate,
  onCreated,
}: {
  open: boolean
  saving: boolean
  vaults: Vault[]
  onClose: () => void
  onCreate: (payload: S3GatewayCredentialCreatePayload) => Promise<{ credential: S3GatewayCredential; secret_access_key: string }>
  onCreated: (credentialId: number) => void
}) {
  const [newName, setNewName] = useState('')
  const [newPrincipal, setNewPrincipal] = useState('')
  const [newScopeMode, setNewScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [newDescription, setNewDescription] = useState('')
  const [newExpiresDays, setNewExpiresDays] = useState('')
  const [newVaultIds, setNewVaultIds] = useState<number[]>([])
  const [newPerms, setNewPerms] = useState({ can_list: true, can_read: true, can_write: false, can_delete: false, can_admin: false })

  if (!open) return null

  const buildNewVaultScopes = () => (
    newScopeMode === 'vault_allowlist'
      ? newVaultIds.map(vault_id => ({ vault_id, ...newPerms }))
      : []
  )

  const submitCreate = async () => {
    const expires = Number(newExpiresDays)
    const expires_at = Number.isFinite(expires) && expires > 0
      ? Math.floor(Date.now() / 1000) + Math.floor(expires * 86400)
      : null
    const principal = Number(newPrincipal)
    const result = await onCreate({
      name: newName,
      principal_user_id: Number.isFinite(principal) && principal > 0 ? principal : null,
      scope_mode: newScopeMode,
      description: newDescription.trim() || null,
      expires_at,
      vault_scopes: buildNewVaultScopes(),
    })
    onCreated(result.credential.id)
    onClose()
    setNewName('')
    setNewPrincipal('')
    setNewDescription('')
    setNewExpiresDays('')
    setNewVaultIds([])
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
      <div className="max-h-[90vh] w-full max-w-3xl overflow-y-auto rounded border border-white/10 bg-zinc-950 p-5 text-white shadow-2xl">
        <div className="mb-4 flex items-center justify-between gap-3">
          <h2 className="text-lg font-semibold text-cyan-100">Create credential</h2>
          <button className={buttonClass} type="button" onClick={onClose}>Close</button>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          <label className="flex flex-col gap-1 text-xs text-white/60">
            Name
            <input className={fieldClass} value={newName} onChange={event => setNewName(event.target.value)} />
          </label>
          <label className="flex flex-col gap-1 text-xs text-white/60">
            Principal user id
            <input className={fieldClass} inputMode="numeric" value={newPrincipal} onChange={event => setNewPrincipal(event.target.value)} />
          </label>
          <label className="flex flex-col gap-1 text-xs text-white/60">
            Scope
            <select className={fieldClass} value={newScopeMode} onChange={event => setNewScopeMode(event.target.value as S3GatewayCredentialScopeMode)}>
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
        </div>

        {newScopeMode === 'vault_allowlist' && (
          <div className="mt-4 space-y-3">
            <div className="flex flex-wrap gap-2">
              {permissionKeys.map(key => (
                <PermissionCheckbox
                  key={key}
                  label={key.replace('can_', '')}
                  checked={!!newPerms[key]}
                  onChange={checked => setNewPerms(perms => ({ ...perms, [key]: checked }))}
                />
              ))}
            </div>
            <div className="grid gap-2 md:grid-cols-3">
              {vaults.map(vault => (
                <label key={vault.id} className="flex min-h-10 items-center gap-2 rounded border border-white/10 bg-white/[0.03] px-3 text-sm text-white/75">
                  <input
                    className="h-4 w-4 accent-cyan-400"
                    type="checkbox"
                    checked={newVaultIds.includes(vault.id)}
                    onChange={event => {
                      setNewVaultIds(ids => event.target.checked ? [...ids, vault.id] : ids.filter(id => id !== vault.id))
                    }}
                  />
                  {vault.name} <span className="text-white/35">#{vault.id}</span>
                </label>
              ))}
            </div>
          </div>
        )}

        <div className="mt-5 flex justify-end gap-2">
          <button className={buttonClass} type="button" onClick={onClose}>Cancel</button>
          <button className={primaryButtonClass} type="button" disabled={!newName.trim() || saving} onClick={() => void submitCreate()}>
            <PlusIcon className="h-4 w-4" />
            Create
          </button>
        </div>
      </div>
    </div>
  )
}
