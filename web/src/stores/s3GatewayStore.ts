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
  S3GatewayCredentialScopeUpdatePayload,
  S3GatewayCredentialVaultScope,
  S3GatewayStatus,
} from '@/models/s3Gateway'
import { useWebSocketStore } from '@/stores/useWebSocket'

const errorMessage = (error: unknown, fallback: string) => (error instanceof Error ? error.message : fallback)

interface S3GatewayStore {
  status: S3GatewayStatus | null
  credentials: S3GatewayCredential[]
  scopesByCredentialId: Record<number, S3GatewayCredentialVaultScope[]>
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
