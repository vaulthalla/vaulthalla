import { create } from 'zustand'
import {
  PriceBudgetPolicy,
  PriceBudgetPolicyPayload,
  PriceBudgetPreflightPayload,
  PriceBudgetPreflightResult,
  PriceBudgetScope,
  PriceBudgetStatus,
} from '@/models/pricing/priceBudget'
import { PriceBudgetLedgerEntry } from '@/models/pricing/priceBudgetLedger'
import { PriceNotification } from '@/models/pricing/priceNotification'
import { PriceOverride, PriceOverrideRequestPayload } from '@/models/pricing/priceOverride'
import { PricingBudgetStats } from '@/models/stats/pricingBudgetStats'
import { useWebSocketStore } from '@/stores/useWebSocket'

type ScopedPayload = { vault_id?: number | null; gateway_credential_id?: number | null }

interface PricingStore {
  policies: PriceBudgetPolicy[]
  ledger: PriceBudgetLedgerEntry[]
  notifications: PriceNotification[]
  overrides: PriceOverride[]
  status: PriceBudgetStatus | null
  stats: PricingBudgetStats | null
  loading: boolean
  saving: boolean
  error: string | null

  fetchPolicies: (payload?: ScopedPayload & { include_inactive?: boolean }) => Promise<PriceBudgetPolicy[]>
  upsertPolicy: (payload: PriceBudgetPolicyPayload) => Promise<PriceBudgetPolicy>
  disablePolicy: (payload: {
    scope: PriceBudgetScope
    provider_key?: string | null
    vault_id?: number | null
    gateway_credential_id?: number | null
  }) => Promise<boolean>
  fetchLedger: (payload?: ScopedPayload & { limit?: number }) => Promise<PriceBudgetLedgerEntry[]>
  fetchStatus: (payload?: ScopedPayload & { limit?: number; include_inactive?: boolean }) => Promise<PriceBudgetStatus>
  preflight: (payload: PriceBudgetPreflightPayload) => Promise<PriceBudgetPreflightResult>
  requestOverride: (payload: PriceOverrideRequestPayload) => Promise<PriceOverride>
  approveOverride: (id: number) => Promise<PriceOverride>
  denyOverride: (id: number, reason?: string | null) => Promise<PriceOverride>
  fetchOverrides: (payload?: ScopedPayload & { limit?: number; include_expired?: boolean }) => Promise<PriceOverride[]>
  fetchNotifications: (payload?: ScopedPayload & { limit?: number; include_acknowledged?: boolean }) => Promise<PriceNotification[]>
  ackNotification: (payload: { id: number; vault_id?: number | null }) => Promise<PriceNotification>
  fetchStats: (payload?: ScopedPayload) => Promise<PricingBudgetStats>
}

const errorMessage = (error: unknown, fallback: string) => (error instanceof Error ? error.message : fallback)

export const usePricingStore = create<PricingStore>()((set, get) => ({
  policies: [],
  ledger: [],
  notifications: [],
  overrides: [],
  status: null,
  stats: null,
  loading: false,
  saving: false,
  error: null,

  async fetchPolicies(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.policy.list', payload ?? null)
      const policies = response.policies.map(PriceBudgetPolicy.from)
      set({ policies, loading: false })
      return policies
    } catch (error) {
      set({ loading: false, error: errorMessage(error, 'Unable to fetch S3 price budget policies') })
      throw error
    }
  },

  async upsertPolicy(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.policy.upsert', payload)
      const policy = PriceBudgetPolicy.from(response.policy)
      set(state => ({
        policies: [policy, ...state.policies.filter(existing => existing.id !== policy.id)],
        saving: false,
      }))
      void get().fetchStats(payload.vault_id ? { vault_id: payload.vault_id } : undefined).catch(() => undefined)
      return policy
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to save S3 price budget policy') })
      throw error
    }
  },

  async disablePolicy(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.policy.disable', payload)
      set({ saving: false })
      await get().fetchPolicies(payload.vault_id ? { vault_id: payload.vault_id } : undefined).catch(() => undefined)
      return response.disabled
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to disable S3 price budget policy') })
      throw error
    }
  },

  async fetchLedger(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    try {
      const response = await ws.sendCommand('pricing.budget.ledger.list', payload ?? null)
      const ledger = response.ledger.map(PriceBudgetLedgerEntry.from)
      set({ ledger })
      return ledger
    } catch (error) {
      set({ error: errorMessage(error, 'Unable to fetch S3 price budget ledger') })
      throw error
    }
  },

  async fetchStatus(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.status', payload ?? null)
      const status = PriceBudgetStatus.from(response)
      set({
        status,
        policies: status.policies,
        ledger: status.ledger,
        notifications: status.notifications,
        overrides: status.overrides,
        loading: false,
      })
      return status
    } catch (error) {
      set({ loading: false, error: errorMessage(error, 'Unable to fetch S3 price budget status') })
      throw error
    }
  },

  async preflight(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('pricing.budget.preflight', payload)
    return PriceBudgetPreflightResult.from(response)
  },

  async requestOverride(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.override.request', payload)
      const override = PriceOverride.from(response.override)
      set(state => ({ overrides: [override, ...state.overrides.filter(existing => existing.id !== override.id)], saving: false }))
      await get().fetchNotifications({ vault_id: payload.vault_id, limit: 50 }).catch(() => undefined)
      return override
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to request S3 price budget override') })
      throw error
    }
  },

  async approveOverride(id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.override.approve', { id })
      const override = PriceOverride.from(response.override)
      set(state => ({
        overrides: state.overrides.map(existing => (existing.id === override.id ? override : existing)),
        saving: false,
      }))
      await get().fetchNotifications({ limit: 50 }).catch(() => undefined)
      return override
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to approve S3 price budget override') })
      throw error
    }
  },

  async denyOverride(id, reason) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('pricing.budget.override.deny', { id, reason })
      const override = PriceOverride.from(response.override)
      set(state => ({
        overrides: state.overrides.map(existing => (existing.id === override.id ? override : existing)),
        saving: false,
      }))
      await get().fetchNotifications({ limit: 50 }).catch(() => undefined)
      return override
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to deny S3 price budget override') })
      throw error
    }
  },

  async fetchOverrides(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    try {
      const response = await ws.sendCommand('pricing.budget.override.list', payload ?? null)
      const overrides = response.overrides.map(PriceOverride.from)
      set({ overrides })
      return overrides
    } catch (error) {
      set({ error: errorMessage(error, 'Unable to fetch S3 price budget overrides') })
      throw error
    }
  },

  async fetchNotifications(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    try {
      const response = await ws.sendCommand('pricing.notifications.list', payload ?? null)
      const notifications = response.notifications.map(PriceNotification.from)
      set({ notifications })
      return notifications
    } catch (error) {
      set({ error: errorMessage(error, 'Unable to fetch pricing notifications') })
      throw error
    }
  },

  async ackNotification(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('pricing.notifications.ack', payload)
    const notification = PriceNotification.from(response.notification)
    set(state => ({
      notifications: state.notifications.map(existing => (existing.id === notification.id ? notification : existing)),
    }))
    return notification
  },

  async fetchStats(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.pricing.budget', payload ?? null)
    const stats = PricingBudgetStats.from(response.stats)
    set({ stats })
    return stats
  },
}))
