import { create } from 'zustand'
import { PriceBudgetPolicy, PriceBudgetPolicyPayload, PriceBudgetStatus } from '@/models/pricing/priceBudget'
import { PriceBudgetLedgerEntry } from '@/models/pricing/priceBudgetLedger'
import {
  S3GatewayBucketBindPayload,
  S3GatewayBucketBinding,
  S3GatewayCreateLocalBucketPayload,
  S3GatewayCreateRemoteCachePayload,
  S3GatewayCredential,
  S3GatewayCredentialCreatePayload,
  S3GatewayCredentialDefaultVaultRole,
  S3GatewayCredentialDefaultVaultRoleOverride,
  S3GatewayCredentialDefaultVaultRoleOverridePayload,
  S3GatewayCredentialDefaultVaultRolePayload,
  S3GatewayCredentialScopeUpdatePayload,
  S3GatewayCredentialSelectedVault,
  S3GatewayCredentialSelectedVaultPayload,
  S3GatewayCredentialVaultRoleAssignment,
  S3GatewayCredentialVaultRoleAssignmentPayload,
  S3GatewayCredentialVaultRoleOverride,
  S3GatewayCredentialVaultRoleOverridePayload,
  S3GatewayCredentialVaultScope,
  S3GatewayStatus,
} from '@/models/s3Gateway'
import { useWebSocketStore } from '@/stores/useWebSocket'

const errorMessage = (error: unknown, fallback: string) => (error instanceof Error ? error.message : fallback)
const credentialVaultKey = (credentialId: number, vaultId: number) => `${credentialId}:${vaultId}`

interface S3GatewayStore {
  status: S3GatewayStatus | null
  credentials: S3GatewayCredential[]
  scopesByCredentialId: Record<number, S3GatewayCredentialVaultScope[]>
  defaultRoleByCredentialId: Record<number, S3GatewayCredentialDefaultVaultRole | null>
  selectedVaultsByCredentialId: Record<number, S3GatewayCredentialSelectedVault[]>
  defaultRoleOverridesByCredentialId: Record<number, S3GatewayCredentialDefaultVaultRoleOverride[]>
  roleAssignmentsByCredentialId: Record<number, S3GatewayCredentialVaultRoleAssignment[]>
  roleOverridesByCredentialVault: Record<string, S3GatewayCredentialVaultRoleOverride[]>
  buckets: S3GatewayBucketBinding[]
  policies: PriceBudgetPolicy[]
  ledger: PriceBudgetLedgerEntry[]
  budgetStatus: PriceBudgetStatus | null
  createdSecret: { credential: S3GatewayCredential; secret_access_key: string } | null
  loading: boolean
  saving: boolean
  error: string | null

  fetchStatus: () => Promise<S3GatewayStatus>
  fetchCredentials: (payload?: { include_disabled?: boolean }) => Promise<S3GatewayCredential[]>
  createCredential: (payload: S3GatewayCredentialCreatePayload) => Promise<{ credential: S3GatewayCredential; secret_access_key: string }>
  revokeCredential: (payload: { access_key?: string; name?: string }) => Promise<boolean>
  updateCredentialScope: (payload: S3GatewayCredentialScopeUpdatePayload) => Promise<S3GatewayCredential | null>
  fetchCredentialScopes: (payload: { access_key?: string; name?: string }) => Promise<S3GatewayCredentialVaultScope[]>
  fetchCredentialDefaultRole: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<S3GatewayCredentialDefaultVaultRole | null>
  setCredentialDefaultRole: (payload: S3GatewayCredentialDefaultVaultRolePayload) => Promise<S3GatewayCredentialDefaultVaultRole | null>
  clearCredentialDefaultRole: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<boolean>
  fetchCredentialSelectedVaults: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<S3GatewayCredentialSelectedVault[]>
  replaceCredentialSelectedVaults: (payload: S3GatewayCredentialSelectedVaultPayload) => Promise<S3GatewayCredentialSelectedVault[]>
  addCredentialSelectedVault: (payload: S3GatewayCredentialSelectedVaultPayload) => Promise<S3GatewayCredentialSelectedVault>
  removeCredentialSelectedVault: (payload: S3GatewayCredentialSelectedVaultPayload) => Promise<boolean>
  fetchCredentialDefaultRoleOverrides: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<S3GatewayCredentialDefaultVaultRoleOverride[]>
  addCredentialDefaultRoleOverride: (payload: S3GatewayCredentialDefaultVaultRoleOverridePayload) => Promise<S3GatewayCredentialDefaultVaultRoleOverride>
  removeCredentialDefaultRoleOverride: (payload: S3GatewayCredentialDefaultVaultRoleOverridePayload) => Promise<boolean>
  fetchCredentialRoleAssignments: (payload: { credential_id?: number; access_key?: string; name?: string; credential_name?: string }) => Promise<S3GatewayCredentialVaultRoleAssignment[]>
  assignCredentialVaultRole: (payload: S3GatewayCredentialVaultRoleAssignmentPayload) => Promise<S3GatewayCredentialVaultRoleAssignment>
  revokeCredentialVaultRole: (payload: S3GatewayCredentialVaultRoleAssignmentPayload) => Promise<boolean>
  fetchCredentialVaultRoleOverrides: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<S3GatewayCredentialVaultRoleOverride[]>
  addCredentialVaultRoleOverride: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<S3GatewayCredentialVaultRoleOverride>
  removeCredentialVaultRoleOverride: (payload: S3GatewayCredentialVaultRoleOverridePayload) => Promise<boolean>
  fetchBuckets: () => Promise<S3GatewayBucketBinding[]>
  bindBucket: (payload: S3GatewayBucketBindPayload) => Promise<void>
  unbindBucket: (payload: { bucket_name: string }) => Promise<boolean>
  createLocalBucket: (payload: S3GatewayCreateLocalBucketPayload) => Promise<S3GatewayBucketBinding>
  createRemoteCacheBucket: (payload: S3GatewayCreateRemoteCachePayload) => Promise<S3GatewayBucketBinding>
  fetchPolicies: (payload?: { gateway_credential_id?: number | null; vault_id?: number | null; include_inactive?: boolean }) => Promise<PriceBudgetPolicy[]>
  upsertPolicy: (payload: PriceBudgetPolicyPayload) => Promise<PriceBudgetPolicy>
  disablePolicy: (payload: PriceBudgetPolicyPayload) => Promise<boolean>
  fetchLedger: (payload?: { gateway_credential_id?: number | null; vault_id?: number | null; limit?: number }) => Promise<PriceBudgetLedgerEntry[]>
  fetchBudgetStatus: (payload?: { gateway_credential_id?: number | null; vault_id?: number | null; limit?: number }) => Promise<PriceBudgetStatus>
  clearCreatedSecret: () => void
}

export const useS3GatewayStore = create<S3GatewayStore>()((set, get) => ({
  status: null,
  credentials: [],
  scopesByCredentialId: {},
  defaultRoleByCredentialId: {},
  selectedVaultsByCredentialId: {},
  defaultRoleOverridesByCredentialId: {},
  roleAssignmentsByCredentialId: {},
  roleOverridesByCredentialVault: {},
  buckets: [],
  policies: [],
  ledger: [],
  budgetStatus: null,
  createdSecret: null,
  loading: false,
  saving: false,
  error: null,

  async fetchStatus() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.status', null)
      const status = S3GatewayStatus.from(response.status)
      set({ status, loading: false })
      return status
    } catch (error) {
      set({ loading: false, error: errorMessage(error, 'Unable to fetch S3 gateway status') })
      throw error
    }
  },

  async fetchCredentials(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.list', payload ?? null)
    const credentials = response.credentials.map(S3GatewayCredential.from)
    set({ credentials })
    return credentials
  },

  async createCredential(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.create', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const createdSecret = { credential, secret_access_key: response.secret_access_key }
      set(state => ({
        credentials: [credential, ...state.credentials.filter(existing => existing.id !== credential.id)],
        createdSecret,
        saving: false,
      }))
      return createdSecret
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to create S3 gateway credential') })
      throw error
    }
  },

  async revokeCredential(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.revoke', payload)
    await get().fetchCredentials().catch(() => undefined)
    return response.revoked
  },

  async updateCredentialScope(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.scope.update', payload)
      const credential = response.credential ? S3GatewayCredential.from(response.credential) : null
      set(state => ({
        credentials: credential ? state.credentials.map(existing => (existing.id === credential.id ? credential : existing)) : state.credentials,
        saving: false,
      }))
      if (payload.vault_scopes && credential) {
        await get().fetchCredentialScopes({ access_key: credential.access_key }).catch(() => undefined)
      }
      if (payload.default_vault_role_id !== undefined && credential) {
        await get().fetchCredentialDefaultRole({ access_key: credential.access_key }).catch(() => undefined)
      }
      if (payload.selected_vault_ids && credential) {
        await get().fetchCredentialSelectedVaults({ access_key: credential.access_key }).catch(() => undefined)
      }
      return credential
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to update S3 gateway credential scope') })
      throw error
    }
  },

  async fetchCredentialScopes(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.scope.list', payload)
    const credential = S3GatewayCredential.from(response.credential)
    const scopes = response.scopes.map(S3GatewayCredentialVaultScope.from)
    set(state => ({ scopesByCredentialId: { ...state.scopesByCredentialId, [credential.id]: scopes } }))
    return scopes
  },

  async fetchCredentialDefaultRole(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.defaultRole.get', payload)
    const credential = S3GatewayCredential.from(response.credential)
    const defaultRole = response.default_role ? S3GatewayCredentialDefaultVaultRole.from(response.default_role) : null
    set(state => ({ defaultRoleByCredentialId: { ...state.defaultRoleByCredentialId, [credential.id]: defaultRole } }))
    return defaultRole
  },

  async setCredentialDefaultRole(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.defaultRole.set', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const defaultRole = response.default_role ? S3GatewayCredentialDefaultVaultRole.from(response.default_role) : null
      set(state => ({
        defaultRoleByCredentialId: { ...state.defaultRoleByCredentialId, [credential.id]: defaultRole },
        saving: false,
      }))
      return defaultRole
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to set S3 gateway credential default role') })
      throw error
    }
  },

  async clearCredentialDefaultRole(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.defaultRole.clear', payload)
      const credential = S3GatewayCredential.from(response.credential)
      set(state => ({
        defaultRoleByCredentialId: { ...state.defaultRoleByCredentialId, [credential.id]: null },
        defaultRoleOverridesByCredentialId: { ...state.defaultRoleOverridesByCredentialId, [credential.id]: [] },
        saving: false,
      }))
      return response.cleared
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to clear S3 gateway credential default role') })
      throw error
    }
  },

  async fetchCredentialSelectedVaults(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.selectedVaults.list', payload)
    const credential = S3GatewayCredential.from(response.credential)
    const selectedVaults = (response.selected_vaults ?? response.vaults ?? []).map(S3GatewayCredentialSelectedVault.from)
    set(state => ({ selectedVaultsByCredentialId: { ...state.selectedVaultsByCredentialId, [credential.id]: selectedVaults } }))
    return selectedVaults
  },

  async replaceCredentialSelectedVaults(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.selectedVaults.replace', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const selectedVaults = (response.selected_vaults ?? response.vaults ?? []).map(S3GatewayCredentialSelectedVault.from)
      set(state => ({
        selectedVaultsByCredentialId: { ...state.selectedVaultsByCredentialId, [credential.id]: selectedVaults },
        saving: false,
      }))
      return selectedVaults
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to replace S3 gateway selected vaults') })
      throw error
    }
  },

  async addCredentialSelectedVault(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.selectedVaults.add', payload)
      const selectedVault = S3GatewayCredentialSelectedVault.from(response.selected_vault)
      set(state => ({
        selectedVaultsByCredentialId: {
          ...state.selectedVaultsByCredentialId,
          [selectedVault.credential_id]: [
            selectedVault,
            ...(state.selectedVaultsByCredentialId[selectedVault.credential_id] ?? []).filter(existing => existing.vault_id !== selectedVault.vault_id),
          ],
        },
        saving: false,
      }))
      return selectedVault
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to add S3 gateway selected vault') })
      throw error
    }
  },

  async removeCredentialSelectedVault(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.selectedVaults.remove', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const vaultId = payload.vault_id ?? response.vault?.id ?? 0
      set(state => ({
        selectedVaultsByCredentialId: {
          ...state.selectedVaultsByCredentialId,
          [credential.id]: (state.selectedVaultsByCredentialId[credential.id] ?? []).filter(selected => selected.vault_id !== vaultId),
        },
        saving: false,
      }))
      return response.removed
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to remove S3 gateway selected vault') })
      throw error
    }
  },

  async fetchCredentialDefaultRoleOverrides(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.defaultRole.overrides.list', payload)
    const credential = S3GatewayCredential.from(response.credential)
    const overrides = response.overrides.map(S3GatewayCredentialDefaultVaultRoleOverride.from)
    set(state => ({ defaultRoleOverridesByCredentialId: { ...state.defaultRoleOverridesByCredentialId, [credential.id]: overrides } }))
    return overrides
  },

  async addCredentialDefaultRoleOverride(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.defaultRole.overrides.add', payload)
      const override = S3GatewayCredentialDefaultVaultRoleOverride.from(response.override)
      set(state => ({
        defaultRoleOverridesByCredentialId: {
          ...state.defaultRoleOverridesByCredentialId,
          [override.credential_id]: [
            override,
            ...(state.defaultRoleOverridesByCredentialId[override.credential_id] ?? []).filter(existing => existing.id !== override.id),
          ],
        },
        saving: false,
      }))
      return override
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to add S3 gateway default role override') })
      throw error
    }
  },

  async removeCredentialDefaultRoleOverride(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.defaultRole.overrides.remove', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const overrideId = payload.override_id ?? payload.id ?? 0
      set(state => ({
        defaultRoleOverridesByCredentialId: {
          ...state.defaultRoleOverridesByCredentialId,
          [credential.id]: (state.defaultRoleOverridesByCredentialId[credential.id] ?? []).filter(override => override.id !== overrideId),
        },
        saving: false,
      }))
      return response.removed
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to remove S3 gateway default role override') })
      throw error
    }
  },

  async fetchCredentialRoleAssignments(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.roles.list', payload)
    const credential = S3GatewayCredential.from(response.credential)
    const roles = (response.roles ?? response.assignments ?? []).map(S3GatewayCredentialVaultRoleAssignment.from)
    set(state => ({ roleAssignmentsByCredentialId: { ...state.roleAssignmentsByCredentialId, [credential.id]: roles } }))
    return roles
  },

  async assignCredentialVaultRole(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.roles.assign', payload)
      const assignment = S3GatewayCredentialVaultRoleAssignment.from(response.assignment)
      set(state => ({
        roleAssignmentsByCredentialId: {
          ...state.roleAssignmentsByCredentialId,
          [assignment.credential_id]: [
            assignment,
            ...(state.roleAssignmentsByCredentialId[assignment.credential_id] ?? []).filter(existing => existing.vault_id !== assignment.vault_id),
          ],
        },
        saving: false,
      }))
      return assignment
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to assign S3 gateway credential vault role') })
      throw error
    }
  },

  async revokeCredentialVaultRole(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.roles.revoke', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const vaultId = payload.vault_id ?? response.vault?.id ?? 0
      set(state => {
        const nextAssignments = (state.roleAssignmentsByCredentialId[credential.id] ?? []).filter(assignment => assignment.vault_id !== vaultId)
        const nextOverrides = { ...state.roleOverridesByCredentialVault }
        if (vaultId) delete nextOverrides[credentialVaultKey(credential.id, vaultId)]
        return {
          roleAssignmentsByCredentialId: { ...state.roleAssignmentsByCredentialId, [credential.id]: nextAssignments },
          roleOverridesByCredentialVault: nextOverrides,
          saving: false,
        }
      })
      return response.revoked
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to revoke S3 gateway credential vault role') })
      throw error
    }
  },

  async fetchCredentialVaultRoleOverrides(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.credentials.roles.overrides.list', payload)
    const credential = S3GatewayCredential.from(response.credential)
    const vaultId = payload.vault_id ?? response.vault?.id ?? 0
    const overrides = response.overrides.map(S3GatewayCredentialVaultRoleOverride.from)
    set(state => ({
      roleOverridesByCredentialVault: {
        ...state.roleOverridesByCredentialVault,
        [credentialVaultKey(credential.id, vaultId)]: overrides,
      },
    }))
    return overrides
  },

  async addCredentialVaultRoleOverride(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.roles.overrides.add', payload)
      const override = S3GatewayCredentialVaultRoleOverride.from(response.override)
      set(state => {
        const key = credentialVaultKey(override.credential_id, override.vault_id)
        return {
          roleOverridesByCredentialVault: {
            ...state.roleOverridesByCredentialVault,
            [key]: [override, ...(state.roleOverridesByCredentialVault[key] ?? []).filter(existing => existing.id !== override.id)],
          },
          saving: false,
        }
      })
      return override
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to add S3 gateway credential role override') })
      throw error
    }
  },

  async removeCredentialVaultRoleOverride(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.credentials.roles.overrides.remove', payload)
      const credential = S3GatewayCredential.from(response.credential)
      const vaultId = payload.vault_id ?? response.vault?.id ?? 0
      const overrideId = payload.override_id ?? payload.id ?? 0
      set(state => {
        const key = credentialVaultKey(credential.id, vaultId)
        return {
          roleOverridesByCredentialVault: {
            ...state.roleOverridesByCredentialVault,
            [key]: (state.roleOverridesByCredentialVault[key] ?? []).filter(override => override.id !== overrideId),
          },
          saving: false,
        }
      })
      return response.removed
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to remove S3 gateway credential role override') })
      throw error
    }
  },

  async fetchBuckets() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.buckets.list', null)
    const buckets = response.buckets.map(S3GatewayBucketBinding.from)
    set({ buckets })
    return buckets
  },

  async bindBucket(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    await ws.sendCommand('s3.gateway.buckets.bind', payload)
    await get().fetchBuckets()
  },

  async unbindBucket(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.buckets.unbind', payload)
    await get().fetchBuckets().catch(() => undefined)
    return response.unbound
  },

  async createLocalBucket(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.buckets.createLocal', payload)
    const bucket = S3GatewayBucketBinding.from(response.bucket)
    set(state => ({ buckets: [bucket, ...state.buckets.filter(existing => existing.bucket_name !== bucket.bucket_name)] }))
    return bucket
  },

  async createRemoteCacheBucket(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.buckets.createRemoteCache', payload)
    const bucket = S3GatewayBucketBinding.from(response.bucket)
    set(state => ({ buckets: [bucket, ...state.buckets.filter(existing => existing.bucket_name !== bucket.bucket_name)] }))
    return bucket
  },

  async fetchPolicies(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.budget.policy.list', payload ?? null)
    const policies = response.policies.map(PriceBudgetPolicy.from)
    set({ policies })
    return policies
  },

  async upsertPolicy(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.budget.policy.upsert', payload)
      const policy = PriceBudgetPolicy.from(response.policy)
      set(state => ({ policies: [policy, ...state.policies.filter(existing => existing.id !== policy.id)], saving: false }))
      await get().fetchBudgetStatus({
        gateway_credential_id: payload.gateway_credential_id ?? null,
        vault_id: payload.vault_id ?? null,
        limit: 25,
      }).catch(() => undefined)
      return policy
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to save S3 gateway budget policy') })
      throw error
    }
  },

  async disablePolicy(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('s3.gateway.budget.policy.disable', payload)
      set({ saving: false })
      await get().fetchPolicies({
        gateway_credential_id: payload.gateway_credential_id ?? null,
        vault_id: payload.vault_id ?? null,
      }).catch(() => undefined)
      await get().fetchBudgetStatus({
        gateway_credential_id: payload.gateway_credential_id ?? null,
        vault_id: payload.vault_id ?? null,
        limit: 25,
      }).catch(() => undefined)
      return response.disabled
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to disable S3 gateway budget policy') })
      throw error
    }
  },

  async fetchLedger(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.budget.ledger.list', payload ?? null)
    const ledger = response.ledger.map(PriceBudgetLedgerEntry.from)
    set({ ledger })
    return ledger
  },

  async fetchBudgetStatus(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('s3.gateway.budget.status', payload ?? null)
    const budgetStatus = PriceBudgetStatus.from(response)
    set({ budgetStatus, policies: budgetStatus.policies, ledger: budgetStatus.ledger })
    return budgetStatus
  },

  clearCreatedSecret() {
    set({ createdSecret: null })
  },
}))
