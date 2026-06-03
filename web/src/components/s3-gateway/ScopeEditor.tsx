'use client'

import { useEffect, useMemo, useState } from 'react'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import PlusIcon from '@/fa-duotone/plus.svg'
import ShieldIcon from '@/fa-duotone/shield-check.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import type { Permission, VaultRole } from '@/models/role'
import {
  S3GatewayCredential,
  S3GatewayCredentialDefaultVaultRole,
  S3GatewayCredentialDefaultVaultRoleOverride,
  S3GatewayCredentialDefaultVaultRoleOverridePayload,
  S3GatewayCredentialDefaultVaultRolePayload,
  S3GatewayCredentialScopeMode,
  S3GatewayCredentialScopeUpdatePayload,
  S3GatewayCredentialSelectedVault,
  S3GatewayCredentialSelectedVaultPayload,
  S3GatewayCredentialVaultRoleAssignment,
  S3GatewayCredentialVaultRoleAssignmentPayload,
  S3GatewayCredentialVaultRoleOverride,
  S3GatewayCredentialVaultRoleOverridePayload,
} from '@/models/s3Gateway'
import type { Vault } from '@/models/vaults'
import {
  dangerButtonClass,
  fieldClass,
  primaryButtonClass,
  scopeLabel,
  scopeModes,
  Section,
} from './shared'

const credentialVaultKey = (credentialId: number, vaultId: number) => `${credentialId}:${vaultId}`

export function ScopeEditor({
  selectedCredential,
  defaultRole,
  selectedVaults,
  defaultRoleOverrides,
  roleAssignments,
  roleOverridesByCredentialVault,
  vaultRoles,
  vaults,
  permissions,
  saving,
  onSave,
  onSetDefaultRole,
  onClearDefaultRole,
  onAddSelectedVault,
  onRemoveSelectedVault,
  onFetchDefaultOverrides,
  onAddDefaultOverride,
  onRemoveDefaultOverride,
  onAssignRole,
  onRevokeRole,
  onFetchOverrides,
  onAddOverride,
  onRemoveOverride,
}: {
  selectedCredential: S3GatewayCredential | null
  defaultRole: S3GatewayCredentialDefaultVaultRole | null
  selectedVaults: S3GatewayCredentialSelectedVault[]
  defaultRoleOverrides: S3GatewayCredentialDefaultVaultRoleOverride[]
  roleAssignments: S3GatewayCredentialVaultRoleAssignment[]
  roleOverridesByCredentialVault: Record<string, S3GatewayCredentialVaultRoleOverride[]>
  vaultRoles: VaultRole[]
  vaults: Vault[]
  permissions: Permission[]
  saving: boolean
  onSave: (payload: S3GatewayCredentialScopeUpdatePayload) => Promise<S3GatewayCredential | null>
  onSetDefaultRole: (payload: S3GatewayCredentialDefaultVaultRolePayload) => Promise<S3GatewayCredentialDefaultVaultRole | null>
  onClearDefaultRole: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<boolean>
  onAddSelectedVault: (payload: S3GatewayCredentialSelectedVaultPayload) => Promise<S3GatewayCredentialSelectedVault>
  onRemoveSelectedVault: (payload: S3GatewayCredentialSelectedVaultPayload) => Promise<boolean>
  onFetchDefaultOverrides: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<S3GatewayCredentialDefaultVaultRoleOverride[]>
  onAddDefaultOverride: (payload: S3GatewayCredentialDefaultVaultRoleOverridePayload) => Promise<S3GatewayCredentialDefaultVaultRoleOverride>
  onRemoveDefaultOverride: (payload: S3GatewayCredentialDefaultVaultRoleOverridePayload) => Promise<boolean>
  onAssignRole: (payload: S3GatewayCredentialVaultRoleAssignmentPayload) => Promise<S3GatewayCredentialVaultRoleAssignment>
  onRevokeRole: (payload: S3GatewayCredentialVaultRoleAssignmentPayload) => Promise<boolean>
  onFetchOverrides: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<S3GatewayCredentialVaultRoleOverride[]>
  onAddOverride: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<S3GatewayCredentialVaultRoleOverride>
  onRemoveOverride: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<boolean>
}) {
  const [editScopeMode, setEditScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [editDescription, setEditDescription] = useState('')
  const [editEnforceLocalBudget, setEditEnforceLocalBudget] = useState(false)
  const [defaultRoleId, setDefaultRoleId] = useState('')
  const [selectedVaultId, setSelectedVaultId] = useState('')
  const [assignVaultId, setAssignVaultId] = useState('')
  const [assignRoleId, setAssignRoleId] = useState('')
  const [defaultOverridePermission, setDefaultOverridePermission] = useState('')
  const [defaultOverrideGlob, setDefaultOverrideGlob] = useState('**')
  const [defaultOverrideEffect, setDefaultOverrideEffect] = useState<'allow' | 'deny'>('deny')
  const [defaultOverrideEnabled, setDefaultOverrideEnabled] = useState(true)
  const [overrideVaultId, setOverrideVaultId] = useState('')
  const [overridePermission, setOverridePermission] = useState('')
  const [overrideGlob, setOverrideGlob] = useState('**')
  const [overrideEffect, setOverrideEffect] = useState<'allow' | 'deny'>('deny')
  const [overrideEnabled, setOverrideEnabled] = useState(true)
  const [scopeSaveState, setScopeSaveState] = useState<'idle' | 'saving' | 'saved' | 'error'>('idle')

  const vaultById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault])), [vaults])
  const assignedVaultIds = useMemo(() => new Set(roleAssignments.map(assignment => assignment.vault_id)), [roleAssignments])
  const selectedVaultIds = useMemo(() => new Set(selectedVaults.filter(vault => vault.enabled).map(vault => vault.vault_id)), [selectedVaults])
  const exceptionVaults = useMemo(
    () => editScopeMode === 'global'
      ? vaults
      : vaults.filter(vault => selectedVaultIds.has(vault.id)),
    [editScopeMode, selectedVaultIds, vaults],
  )
  const availableVaults = useMemo(() => exceptionVaults.filter(vault => !assignedVaultIds.has(vault.id)), [assignedVaultIds, exceptionVaults])
  const selectableVaults = useMemo(() => vaults.filter(vault => !selectedVaultIds.has(vault.id)), [selectedVaultIds, vaults])
  const vaultFsPermissions = useMemo(
    () => permissions.filter(permission => permission.qualified.startsWith('vault.fs.')),
    [permissions],
  )
  const selectedOverrideVaultId = Number(overrideVaultId)
  const selectedOverrides = selectedCredential && selectedOverrideVaultId
    ? roleOverridesByCredentialVault[credentialVaultKey(selectedCredential.id, selectedOverrideVaultId)] ?? []
    : []

  useEffect(() => {
    if (!selectedCredential) return
    setEditScopeMode(selectedCredential.scope_mode)
    setEditDescription(selectedCredential.description ?? '')
    setEditEnforceLocalBudget(selectedCredential.enforce_budget_for_local_requests)
    setDefaultRoleId(defaultRole?.vault_role_id ? String(defaultRole.vault_role_id) : '')
    setScopeSaveState('idle')
  }, [defaultRole?.vault_role_id, selectedCredential])

  useEffect(() => {
    if (!exceptionVaults.length) {
      setOverrideVaultId('')
      return
    }
    if (!exceptionVaults.some(vault => String(vault.id) === overrideVaultId))
      setOverrideVaultId(String(exceptionVaults[0].id))
  }, [exceptionVaults, overrideVaultId])

  useEffect(() => {
    if (!selectedCredential || !selectedOverrideVaultId) return
    void onFetchOverrides({ credential_id: selectedCredential.id, vault_id: selectedOverrideVaultId }).catch(() => undefined)
  }, [onFetchOverrides, selectedCredential, selectedOverrideVaultId])

  useEffect(() => {
    if (!selectedCredential) return
    void onFetchDefaultOverrides({ credential_id: selectedCredential.id }).catch(() => undefined)
  }, [onFetchDefaultOverrides, selectedCredential])

  useEffect(() => {
    if (!assignRoleId && vaultRoles.length) setAssignRoleId(String(vaultRoles[0].id))
  }, [assignRoleId, vaultRoles])

  useEffect(() => {
    if (!overridePermission && vaultFsPermissions.length) setOverridePermission(vaultFsPermissions[0].qualified)
  }, [overridePermission, vaultFsPermissions])

  useEffect(() => {
    if (!defaultOverridePermission && vaultFsPermissions.length) setDefaultOverridePermission(vaultFsPermissions[0].qualified)
  }, [defaultOverridePermission, vaultFsPermissions])

  const saveScope = async () => {
    if (!selectedCredential) return
    setScopeSaveState('saving')
    try {
      await onSave({
        access_key: selectedCredential.access_key,
        scope_mode: editScopeMode,
        description: editDescription.trim() || null,
        ...(editScopeMode !== 'user_access' && defaultRoleId ? { default_vault_role_id: Number(defaultRoleId) } : {}),
        ...(editScopeMode === 'vault_allowlist' ? { selected_vault_ids: selectedVaults.filter(vault => vault.enabled).map(vault => vault.vault_id) } : {}),
        enforce_budget_for_local_requests: editEnforceLocalBudget,
      })
      setScopeSaveState('saved')
    } catch (error) {
      setScopeSaveState('error')
      throw error
    }
  }

  const setDefaultRole = async () => {
    if (!selectedCredential || !defaultRoleId) return
    await onSetDefaultRole({
      credential_id: selectedCredential.id,
      vault_role_id: Number(defaultRoleId),
      enabled: true,
    })
  }

  const clearDefaultRole = async () => {
    if (!selectedCredential) return
    await onClearDefaultRole({ credential_id: selectedCredential.id })
    setDefaultRoleId('')
  }

  const addSelectedVault = async () => {
    if (!selectedCredential || !selectedVaultId) return
    await onAddSelectedVault({
      credential_id: selectedCredential.id,
      vault_id: Number(selectedVaultId),
      enabled: true,
    })
    setSelectedVaultId('')
  }

  const addDefaultOverride = async () => {
    if (!selectedCredential || !defaultOverridePermission.trim() || !defaultOverrideGlob.trim()) return
    await onAddDefaultOverride({
      credential_id: selectedCredential.id,
      permission_qualified: defaultOverridePermission,
      glob_path: defaultOverrideGlob.trim(),
      effect: defaultOverrideEffect,
      enabled: defaultOverrideEnabled,
    })
  }

  const assignRole = async () => {
    if (!selectedCredential || !assignVaultId || !assignRoleId) return
    await onAssignRole({
      credential_id: selectedCredential.id,
      vault_id: Number(assignVaultId),
      vault_role_id: Number(assignRoleId),
      enabled: true,
    })
    setAssignVaultId('')
  }

  const addOverride = async () => {
    if (!selectedCredential || !selectedOverrideVaultId || !overridePermission.trim() || !overrideGlob.trim()) return
    await onAddOverride({
      credential_id: selectedCredential.id,
      vault_id: selectedOverrideVaultId,
      permission_qualified: overridePermission,
      glob_path: overrideGlob.trim(),
      effect: overrideEffect,
      enabled: overrideEnabled,
    })
  }

  return (
    <Section title="Credential Roles" icon={ShieldIcon}>
      {selectedCredential ? (
        <div className="space-y-5">
          <div className="grid gap-3 md:grid-cols-[220px_1fr_auto]">
            <label className="flex flex-col gap-1 text-xs text-white/60">
              Scope mode
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
          <div className="min-h-5 text-xs text-white/45" data-testid="s3-gateway-scope-save-status" aria-live="polite">
            {scopeSaveState === 'saving' ? 'Saving' : scopeSaveState === 'saved' ? 'Saved' : scopeSaveState === 'error' ? 'Save failed' : ''}
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
                Local and cache hits are treated as gateway key usage when this is enabled.
              </span>
            </span>
          </label>

          {editScopeMode === 'user_access' ? (
            <div className="rounded border border-white/10 bg-white/[0.03] p-3 text-sm leading-6 text-white/70" data-testid="s3-gateway-user-access-policy-note">
              This key uses the principal user&apos;s existing Vaulthalla vault roles and overrides. No gateway vault-role policy is configured for this key.
            </div>
          ) : (
            <div className="space-y-5">
              <div className="rounded border border-white/10 bg-white/[0.03] p-3">
                <div className="mb-3 text-sm font-medium text-cyan-100">Default vault role for this key</div>
                <div className="grid gap-3 md:grid-cols-[1fr_auto_auto]">
                  <label className="flex flex-col gap-1 text-xs text-white/60">
                    Vault role
                    <select className={fieldClass} data-testid="s3-gateway-default-role-select" value={defaultRoleId} onChange={event => setDefaultRoleId(event.target.value)}>
                      <option value="">Select default role</option>
                      {vaultRoles.map(role => <option key={role.id} value={role.id}>{role.name}</option>)}
                    </select>
                  </label>
                  <button className={`${primaryButtonClass} self-end`} data-testid="s3-gateway-default-role-save" type="button" disabled={!defaultRoleId || saving} onClick={() => void setDefaultRole()}>
                    <SaveIcon className="h-4 w-4" />
                    Set
                  </button>
                  <button className={`${dangerButtonClass} self-end`} type="button" disabled={!defaultRole || saving} onClick={() => void clearDefaultRole()}>
                    <TrashIcon className="h-4 w-4" />
                    Clear
                  </button>
                </div>
                <div className="mt-2 text-xs text-white/45">
                  {editScopeMode === 'global'
                    ? 'Applies to all gateway bucket bindings unless a per-vault exception overrides it.'
                    : 'Applies to every selected vault unless a per-vault exception overrides it.'}
                </div>
              </div>

              {editScopeMode === 'vault_allowlist' && (
                <div className="rounded border border-white/10 bg-white/[0.03] p-3" data-testid="s3-gateway-selected-vaults-panel">
                  <div className="mb-3 text-sm font-medium text-cyan-100">Selected vaults</div>
                  <div className="grid gap-3">
                    {selectedVaults.map(selectedVault => (
                      <div key={selectedVault.vault_id} className="flex items-center justify-between gap-3 rounded border border-white/10 bg-zinc-950 p-3 text-sm">
                        <div>
                          <div className="font-medium text-white">{selectedVault.vault?.name ?? vaultById.get(selectedVault.vault_id)?.name ?? `Vault ${selectedVault.vault_id}`}</div>
                          <div className="text-xs text-white/45">Vault #{selectedVault.vault_id} · {selectedVault.enabled ? 'enabled' : 'disabled'}</div>
                        </div>
                        <button className={dangerButtonClass} type="button" disabled={saving} onClick={() => void onRemoveSelectedVault({ credential_id: selectedCredential.id, vault_id: selectedVault.vault_id })}>
                          <TrashIcon className="h-4 w-4" />
                          Remove
                        </button>
                      </div>
                    ))}
                    {selectedVaults.length === 0 && (
                      <div className="rounded border border-amber-300/20 bg-amber-400/10 p-3 text-sm text-amber-50">Inactive until a default role and at least one selected vault are configured.</div>
                    )}
                  </div>
                  <div className="mt-3 grid gap-3 md:grid-cols-[1fr_auto]">
                    <label className="flex flex-col gap-1 text-xs text-white/60">
                      Add vault
                      <select className={fieldClass} data-testid="s3-gateway-selected-vault-add-select" value={selectedVaultId} onChange={event => setSelectedVaultId(event.target.value)}>
                        <option value="">Select vault</option>
                        {selectableVaults.map(vault => <option key={vault.id} value={vault.id}>{vault.name} #{vault.id}</option>)}
                      </select>
                    </label>
                    <button className={`${primaryButtonClass} self-end`} data-testid="s3-gateway-selected-vault-add-submit" type="button" disabled={!selectedVaultId || saving} onClick={() => void addSelectedVault()}>
                      <PlusIcon className="h-4 w-4" />
                      Add
                    </button>
                  </div>
                </div>
              )}

              <div className="space-y-3">
                <div className="text-sm font-medium text-cyan-100">Default path overrides</div>
                <div className="overflow-x-auto rounded border border-white/10">
                  <table className="min-w-full text-left text-sm">
                    <thead className="text-xs uppercase tracking-normal text-white/45">
                      <tr>
                        <th className="px-3 py-2">Permission</th>
                        <th className="px-3 py-2">Path</th>
                        <th className="px-3 py-2">Effect</th>
                        <th className="px-3 py-2">Enabled</th>
                        <th className="px-3 py-2"></th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-white/10">
                      {defaultRoleOverrides.map(override => (
                        <tr key={override.id}>
                          <td className="px-3 py-2 text-white">{override.permission_qualified || override.permission_name || override.permission?.qualified}</td>
                          <td className="px-3 py-2 font-mono text-xs text-white/70">{override.glob_path}</td>
                          <td className="px-3 py-2 text-white/70">{override.effect}</td>
                          <td className="px-3 py-2 text-white/70">{override.enabled ? 'yes' : 'no'}</td>
                          <td className="px-3 py-2">
                            <button className={dangerButtonClass} type="button" disabled={saving} onClick={() => void onRemoveDefaultOverride({ credential_id: selectedCredential.id, override_id: override.id })}>
                              <TrashIcon className="h-4 w-4" />
                              Remove
                            </button>
                          </td>
                        </tr>
                      ))}
                      {defaultRoleOverrides.length === 0 && (
                        <tr>
                          <td className="px-3 py-4 text-center text-white/45" colSpan={5}>No default overrides</td>
                        </tr>
                      )}
                    </tbody>
                  </table>
                </div>
                <div className="grid gap-3 rounded border border-white/10 bg-white/[0.03] p-3 md:grid-cols-[1.2fr_1fr_120px_120px_auto]">
                  <label className="flex flex-col gap-1 text-xs text-white/60">
                    Permission
                    <select className={fieldClass} data-testid="s3-gateway-default-override-permission-select" value={defaultOverridePermission} onChange={event => setDefaultOverridePermission(event.target.value)}>
                      {vaultFsPermissions.map(permission => <option key={permission.qualified} value={permission.qualified}>{permission.qualified}</option>)}
                    </select>
                  </label>
                  <label className="flex flex-col gap-1 text-xs text-white/60">
                    Path glob
                    <input className={fieldClass} data-testid="s3-gateway-default-override-glob-input" value={defaultOverrideGlob} onChange={event => setDefaultOverrideGlob(event.target.value)} />
                  </label>
                  <label className="flex flex-col gap-1 text-xs text-white/60">
                    Effect
                    <select className={fieldClass} data-testid="s3-gateway-default-override-effect-select" value={defaultOverrideEffect} onChange={event => setDefaultOverrideEffect(event.target.value as 'allow' | 'deny')}>
                      <option value="deny">deny</option>
                      <option value="allow">allow</option>
                    </select>
                  </label>
                  <label className="flex min-h-10 items-center gap-2 self-end rounded border border-white/10 bg-zinc-950 px-3 text-sm text-white/75">
                    <input className="h-4 w-4 accent-cyan-400" type="checkbox" checked={defaultOverrideEnabled} onChange={event => setDefaultOverrideEnabled(event.target.checked)} />
                    Enabled
                  </label>
                  <button className={`${primaryButtonClass} self-end`} data-testid="s3-gateway-default-override-add-submit" type="button" disabled={!defaultRole || !defaultOverridePermission || !defaultOverrideGlob.trim() || saving} onClick={() => void addDefaultOverride()}>
                    <PlusIcon className="h-4 w-4" />
                    Add
                  </button>
                </div>
              </div>

              <div>
            <div className="mb-2 text-sm font-medium text-cyan-100">Optional per-vault role exceptions</div>
            <div className="grid gap-3">
              {roleAssignments.map(assignment => (
                <div key={assignment.id || assignment.vault_id} className="grid gap-3 rounded border border-white/10 bg-white/[0.03] p-3 lg:grid-cols-[1fr_auto]" data-testid="s3-gateway-role-assignment-row">
                  <div>
                    <div className="font-medium text-white">{assignment.vault?.name ?? vaultById.get(assignment.vault_id)?.name ?? `Vault ${assignment.vault_id}`}</div>
                    <div className="text-xs text-white/45">Vault #{assignment.vault_id} · Role {assignment.role?.name ?? assignment.vault_role_id} · {assignment.enabled ? 'enabled' : 'disabled'}</div>
                  </div>
                  <button className={dangerButtonClass} type="button" disabled={saving} onClick={() => void onRevokeRole({ credential_id: selectedCredential.id, vault_id: assignment.vault_id })}>
                    <TrashIcon className="h-4 w-4" />
                    Revoke
                  </button>
                </div>
              ))}
              {roleAssignments.length === 0 && (
                <div className="rounded border border-amber-300/20 bg-amber-400/10 p-3 text-sm text-amber-50" data-testid="s3-gateway-vault-allowlist-empty">
                  {editScopeMode === 'vault_allowlist' ? 'No per-vault role exceptions.' : 'No per-vault role exceptions.'}
                </div>
              )}
            </div>
          </div>

          <div className="grid gap-3 rounded border border-white/10 bg-white/[0.03] p-3 md:grid-cols-[1fr_1fr_auto]">
            <label className="flex flex-col gap-1 text-xs text-white/60">
              Vault
              <select className={fieldClass} data-testid="s3-gateway-role-assign-vault-select" value={assignVaultId} onChange={event => setAssignVaultId(event.target.value)}>
                <option value="">Select vault</option>
                {availableVaults.map(vault => <option key={vault.id} value={vault.id}>{vault.name} #{vault.id}</option>)}
              </select>
            </label>
            <label className="flex flex-col gap-1 text-xs text-white/60">
              Vault role
              <select className={fieldClass} data-testid="s3-gateway-role-assign-role-select" value={assignRoleId} onChange={event => setAssignRoleId(event.target.value)}>
                {vaultRoles.map(role => <option key={role.id} value={role.id}>{role.name}</option>)}
              </select>
            </label>
            <button className={`${primaryButtonClass} self-end`} data-testid="s3-gateway-role-assign-submit" type="button" disabled={!assignVaultId || !assignRoleId || saving} onClick={() => void assignRole()}>
              <PlusIcon className="h-4 w-4" />
              Assign
            </button>
          </div>

          <div className="space-y-3">
            <div className="flex flex-wrap items-end gap-3">
              <label className="flex min-w-56 flex-col gap-1 text-xs text-white/60">
                Overrides vault
                <select className={fieldClass} data-testid="s3-gateway-override-vault-select" value={overrideVaultId} onChange={event => setOverrideVaultId(event.target.value)}>
                  <option value="">Select vault</option>
                  {exceptionVaults.map(vault => (
                    <option key={vault.id} value={vault.id}>{vault.name} #{vault.id}</option>
                  ))}
                </select>
              </label>
            </div>

            <div className="overflow-x-auto rounded border border-white/10">
              <table className="min-w-full text-left text-sm">
                <thead className="text-xs uppercase tracking-normal text-white/45">
                  <tr>
                    <th className="px-3 py-2">Permission</th>
                    <th className="px-3 py-2">Path</th>
                    <th className="px-3 py-2">Effect</th>
                    <th className="px-3 py-2">Enabled</th>
                    <th className="px-3 py-2"></th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-white/10">
                  {selectedOverrides.map(override => (
                    <tr key={override.id}>
                      <td className="px-3 py-2 text-white">{override.permission_qualified || override.permission_name || override.permission?.qualified}</td>
                      <td className="px-3 py-2 font-mono text-xs text-white/70">{override.glob_path}</td>
                      <td className="px-3 py-2 text-white/70">{override.effect}</td>
                      <td className="px-3 py-2 text-white/70">{override.enabled ? 'yes' : 'no'}</td>
                      <td className="px-3 py-2">
                        <button className={dangerButtonClass} type="button" disabled={saving} onClick={() => void onRemoveOverride({ credential_id: selectedCredential.id, vault_id: selectedOverrideVaultId, override_id: override.id })}>
                          <TrashIcon className="h-4 w-4" />
                          Remove
                        </button>
                      </td>
                    </tr>
                  ))}
                  {selectedOverrides.length === 0 && (
                    <tr>
                      <td className="px-3 py-4 text-center text-white/45" colSpan={5}>No overrides</td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>

            <div className="grid gap-3 rounded border border-white/10 bg-white/[0.03] p-3 md:grid-cols-[1.2fr_1fr_120px_120px_auto]">
              <label className="flex flex-col gap-1 text-xs text-white/60">
                Permission
                <select className={fieldClass} data-testid="s3-gateway-override-permission-select" value={overridePermission} onChange={event => setOverridePermission(event.target.value)}>
                  {vaultFsPermissions.map(permission => <option key={permission.qualified} value={permission.qualified}>{permission.qualified}</option>)}
                </select>
              </label>
              <label className="flex flex-col gap-1 text-xs text-white/60">
                Path glob
                <input className={fieldClass} data-testid="s3-gateway-override-glob-input" value={overrideGlob} onChange={event => setOverrideGlob(event.target.value)} />
              </label>
              <label className="flex flex-col gap-1 text-xs text-white/60">
                Effect
                <select className={fieldClass} data-testid="s3-gateway-override-effect-select" value={overrideEffect} onChange={event => setOverrideEffect(event.target.value as 'allow' | 'deny')}>
                  <option value="deny">deny</option>
                  <option value="allow">allow</option>
                </select>
              </label>
              <label className="flex min-h-10 items-center gap-2 self-end rounded border border-white/10 bg-zinc-950 px-3 text-sm text-white/75">
                <input className="h-4 w-4 accent-cyan-400" type="checkbox" checked={overrideEnabled} onChange={event => setOverrideEnabled(event.target.checked)} />
                Enabled
              </label>
              <button className={`${primaryButtonClass} self-end`} data-testid="s3-gateway-override-add-submit" type="button" disabled={!selectedOverrideVaultId || !overridePermission || !overrideGlob.trim() || saving} onClick={() => void addOverride()}>
                <PlusIcon className="h-4 w-4" />
                Add
              </button>
            </div>
          </div>
            </div>
          )}
        </div>
      ) : (
        <div className="py-6 text-center text-white/50">Select a credential</div>
      )}
    </Section>
  )
}
