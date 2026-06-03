'use client'

import { useEffect, useMemo, useState } from 'react'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import PlusIcon from '@/fa-duotone/plus.svg'
import ShieldIcon from '@/fa-duotone/shield-check.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import type { Permission, VaultRole } from '@/models/role'
import {
  S3GatewayCredential,
  S3GatewayCredentialScopeMode,
  S3GatewayCredentialScopeUpdatePayload,
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
  roleAssignments,
  roleOverridesByCredentialVault,
  vaultRoles,
  vaults,
  permissions,
  saving,
  onSave,
  onAssignRole,
  onRevokeRole,
  onFetchOverrides,
  onAddOverride,
  onRemoveOverride,
}: {
  selectedCredential: S3GatewayCredential | null
  roleAssignments: S3GatewayCredentialVaultRoleAssignment[]
  roleOverridesByCredentialVault: Record<string, S3GatewayCredentialVaultRoleOverride[]>
  vaultRoles: VaultRole[]
  vaults: Vault[]
  permissions: Permission[]
  saving: boolean
  onSave: (payload: S3GatewayCredentialScopeUpdatePayload) => Promise<S3GatewayCredential | null>
  onAssignRole: (payload: S3GatewayCredentialVaultRoleAssignmentPayload) => Promise<S3GatewayCredentialVaultRoleAssignment>
  onRevokeRole: (payload: S3GatewayCredentialVaultRoleAssignmentPayload) => Promise<boolean>
  onFetchOverrides: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<S3GatewayCredentialVaultRoleOverride[]>
  onAddOverride: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<S3GatewayCredentialVaultRoleOverride>
  onRemoveOverride: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<boolean>
}) {
  const [editScopeMode, setEditScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [editDescription, setEditDescription] = useState('')
  const [editEnforceLocalBudget, setEditEnforceLocalBudget] = useState(false)
  const [assignVaultId, setAssignVaultId] = useState('')
  const [assignRoleId, setAssignRoleId] = useState('')
  const [overrideVaultId, setOverrideVaultId] = useState('')
  const [overridePermission, setOverridePermission] = useState('')
  const [overrideGlob, setOverrideGlob] = useState('**')
  const [overrideEffect, setOverrideEffect] = useState<'allow' | 'deny'>('deny')
  const [overrideEnabled, setOverrideEnabled] = useState(true)
  const [scopeSaveState, setScopeSaveState] = useState<'idle' | 'saving' | 'saved' | 'error'>('idle')

  const vaultById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault])), [vaults])
  const assignedVaultIds = useMemo(() => new Set(roleAssignments.map(assignment => assignment.vault_id)), [roleAssignments])
  const availableVaults = useMemo(() => vaults.filter(vault => !assignedVaultIds.has(vault.id)), [assignedVaultIds, vaults])
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
    setScopeSaveState('idle')
  }, [selectedCredential])

  useEffect(() => {
    if (!roleAssignments.length) {
      setOverrideVaultId('')
      return
    }
    if (!roleAssignments.some(assignment => String(assignment.vault_id) === overrideVaultId))
      setOverrideVaultId(String(roleAssignments[0].vault_id))
  }, [overrideVaultId, roleAssignments])

  useEffect(() => {
    if (!selectedCredential || !selectedOverrideVaultId) return
    void onFetchOverrides({ credential_id: selectedCredential.id, vault_id: selectedOverrideVaultId }).catch(() => undefined)
  }, [onFetchOverrides, selectedCredential, selectedOverrideVaultId])

  useEffect(() => {
    if (!assignRoleId && vaultRoles.length) setAssignRoleId(String(vaultRoles[0].id))
  }, [assignRoleId, vaultRoles])

  useEffect(() => {
    if (!overridePermission && vaultFsPermissions.length) setOverridePermission(vaultFsPermissions[0].qualified)
  }, [overridePermission, vaultFsPermissions])

  const saveScope = async () => {
    if (!selectedCredential) return
    setScopeSaveState('saving')
    try {
      await onSave({
        access_key: selectedCredential.access_key,
        scope_mode: editScopeMode,
        description: editDescription.trim() || null,
        enforce_budget_for_local_requests: editEnforceLocalBudget,
      })
      setScopeSaveState('saved')
    } catch (error) {
      setScopeSaveState('error')
      throw error
    }
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

          <div>
            <div className="mb-2 text-sm font-medium text-cyan-100">Vault role assignments</div>
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
                  {selectedCredential.scope_mode === 'vault_allowlist' ? 'Assign at least one vault role.' : 'No vault role assignments.'}
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
                  <option value="">Select assigned vault</option>
                  {roleAssignments.map(assignment => (
                    <option key={assignment.vault_id} value={assignment.vault_id}>{assignment.vault?.name ?? vaultById.get(assignment.vault_id)?.name ?? `Vault ${assignment.vault_id}`}</option>
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
      ) : (
        <div className="py-6 text-center text-white/50">Select a credential</div>
      )}
    </Section>
  )
}
