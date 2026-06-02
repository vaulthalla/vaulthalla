'use client'

import { useEffect, useMemo, useState } from 'react'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import PlusIcon from '@/fa-duotone/plus.svg'
import ShieldIcon from '@/fa-duotone/shield-check.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import type { VaultRole } from '@/models/role'
import {
  S3GatewayCredential,
  S3GatewayCredentialScopeMode,
  S3GatewayCredentialScopeUpdatePayload,
  S3GatewayCredentialVaultScope,
  S3GatewayCredentialVaultScopePayload,
} from '@/models/s3Gateway'
import type { Vault } from '@/models/vaults'
import {
  buttonClass,
  dangerButtonClass,
  fieldClass,
  primaryButtonClass,
  scopeLabel,
  scopeModes,
  scopePayloadForRole,
  scopeRoleName,
  Section,
} from './shared'

type ScopeDraftRow = S3GatewayCredentialVaultScopePayload & { role_name?: string }

export function ScopeEditor({
  selectedCredential,
  selectedScopes,
  vaultRoles,
  vaults,
  saving,
  onSave,
}: {
  selectedCredential: S3GatewayCredential | null
  selectedScopes: S3GatewayCredentialVaultScope[]
  vaultRoles: VaultRole[]
  vaults: Vault[]
  saving: boolean
  onSave: (payload: S3GatewayCredentialScopeUpdatePayload) => Promise<S3GatewayCredential | null>
}) {
  const [editScopeMode, setEditScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [editDescription, setEditDescription] = useState('')
  const [editEnforceLocalBudget, setEditEnforceLocalBudget] = useState(false)
  const [scopeDraft, setScopeDraft] = useState<ScopeDraftRow[]>([])
  const vaultById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault])), [vaults])

  useEffect(() => {
    if (!selectedCredential) return
    setEditScopeMode(selectedCredential.scope_mode)
    setEditDescription(selectedCredential.description ?? '')
    setEditEnforceLocalBudget(selectedCredential.enforce_budget_for_local_requests)
  }, [selectedCredential])

  useEffect(() => {
    setScopeDraft(selectedScopes.map(scope => ({
      vault_id: scope.vault_id,
      can_list: scope.can_list,
      can_read: scope.can_read,
      can_write: scope.can_write,
      can_delete: scope.can_delete,
      can_admin: scope.can_admin,
      role_name: scope.role?.name ?? scopeRoleName(scope),
    })))
  }, [selectedScopes])

  const updateScopeDraftRole = (vaultId: number, roleName: string) => {
    const role = vaultRoles.find(item => item.name === roleName)
    setScopeDraft(rows => rows.map(row => (row.vault_id === vaultId ? { ...row, ...scopePayloadForRole(role, row), role_name: roleName } : row)))
  }

  const addScopeVault = (vaultId: number) => {
    if (scopeDraft.some(row => row.vault_id === vaultId)) return
    const role = vaultRoles.find(item => item.name === 'reader') ?? vaultRoles[0]
    setScopeDraft(rows => [...rows, { vault_id: vaultId, ...scopePayloadForRole(role), role_name: role?.name ?? 'reader' }])
  }

  const saveScope = async () => {
    if (!selectedCredential) return
    await onSave({
      access_key: selectedCredential.access_key,
      scope_mode: editScopeMode,
      description: editDescription.trim() || null,
      enforce_budget_for_local_requests: editEnforceLocalBudget,
      vault_scopes: scopeDraft.map(row => ({
        vault_id: row.vault_id,
        can_list: row.can_list,
        can_read: row.can_read,
        can_write: row.can_write,
        can_delete: row.can_delete,
        can_admin: row.can_admin,
      })),
    })
  }

  return (
    <Section title="Scope Editor" icon={ShieldIcon}>
      {selectedCredential ? (
        <div className="space-y-4">
          <div className="grid gap-3 md:grid-cols-[220px_1fr_auto]">
            <label className="flex flex-col gap-1 text-xs text-white/60">
              Scope
              <select className={fieldClass} data-testid="s3-gateway-scope-editor-scope-select" value={editScopeMode} onChange={event => setEditScopeMode(event.target.value as S3GatewayCredentialScopeMode)}>
                {scopeModes.map(mode => <option key={mode} value={mode}>{scopeLabel(mode)}</option>)}
              </select>
            </label>
            <label className="flex flex-col gap-1 text-xs text-white/60">
              Description
              <input className={fieldClass} value={editDescription} onChange={event => setEditDescription(event.target.value)} />
            </label>
            <button className={`${primaryButtonClass} self-end`} data-testid="s3-gateway-scope-save" type="button" disabled={saving} onClick={() => void saveScope()}>
              <SaveIcon className="h-4 w-4" />
              Save
            </button>
          </div>

          <label className="flex items-start gap-3 rounded border border-white/10 bg-white/[0.03] p-3 text-sm text-white/75">
            <input
              className="mt-1 h-4 w-4 accent-cyan-400"
              data-testid="s3-gateway-scope-enforce-local-budget"
              type="checkbox"
              checked={editEnforceLocalBudget}
              onChange={event => setEditEnforceLocalBudget(event.target.checked)}
            />
            <span>
              <span className="block font-medium text-white">Count local/cache hits against gateway request budgets</span>
              <span className="mt-1 block text-xs leading-5 text-white/55">
                Default off. When off, cache/local hits do not consume upstream-style request budgets. When on, local/cache hits are treated as gateway usage for this key even when no upstream S3/R2 call occurs.
              </span>
            </span>
          </label>

          <div className="grid gap-3">
            {scopeDraft.map(row => (
              <div key={row.vault_id} className="grid gap-3 rounded border border-white/10 bg-white/[0.03] p-3 lg:grid-cols-[1fr_auto]" data-testid="s3-gateway-scope-row">
                <div>
                  <div className="font-medium text-white">{vaultById.get(row.vault_id)?.name ?? `Vault ${row.vault_id}`}</div>
                  <div className="text-xs text-white/45">#{row.vault_id}</div>
                </div>
                <div className="flex flex-wrap items-center gap-2">
                  <select className={fieldClass} data-testid="s3-gateway-scope-role-select" value={row.role_name ?? scopeRoleName(row)} onChange={event => updateScopeDraftRole(row.vault_id, event.target.value)}>
                    {vaultRoles.length ? vaultRoles.map(role => <option key={role.id} value={role.name}>{role.name}</option>) : <option value="reader">reader</option>}
                  </select>
                  <button className={dangerButtonClass} type="button" onClick={() => setScopeDraft(rows => rows.filter(item => item.vault_id !== row.vault_id))}>
                    <TrashIcon className="h-4 w-4" />
                    Remove
                  </button>
                </div>
              </div>
            ))}
          </div>

          <div className="flex flex-wrap gap-2">
            {vaults.filter(vault => !scopeDraft.some(row => row.vault_id === vault.id)).map(vault => (
              <button key={vault.id} className={buttonClass} type="button" onClick={() => addScopeVault(vault.id)}>
                <PlusIcon className="h-4 w-4" />
                {vault.name}
              </button>
            ))}
          </div>
        </div>
      ) : (
        <div className="py-6 text-center text-white/50">Select a credential</div>
      )}
    </Section>
  )
}
