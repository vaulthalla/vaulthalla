import { create } from 'zustand'
import {
  OperatorEmailConfigPatch,
  OperatorEmailConfigResponse,
  OperatorEmailHistoryRecord,
  OperatorEmailSecretGetResponse,
  OperatorEmailSecretPayload,
  OperatorEmailTestPayload,
  OperatorEmailTestResponse,
} from '@/models/operatorEmail'
import { useWebSocketStore } from '@/stores/useWebSocket'

interface OperatorEmailStore {
  config: OperatorEmailConfigResponse | null
  history: OperatorEmailHistoryRecord[]
  loading: boolean
  saving: boolean
  error: string | null
  fetchConfig: () => Promise<OperatorEmailConfigResponse>
  updateConfig: (payload: OperatorEmailConfigPatch) => Promise<OperatorEmailConfigResponse>
  setProviderSecret: (payload: OperatorEmailSecretPayload) => Promise<void>
  fetchSesAccessKey: () => Promise<OperatorEmailSecretGetResponse>
  revealSesSecretAccessKey: () => Promise<OperatorEmailSecretGetResponse>
  sendTest: (payload: OperatorEmailTestPayload) => Promise<OperatorEmailTestResponse>
  fetchHistory: (limit?: number) => Promise<void>
}

const errorMessage = (error: unknown, fallback: string) => (error instanceof Error ? error.message : fallback)

export const useOperatorEmailStore = create<OperatorEmailStore>()((set, get) => ({
  config: null,
  history: [],
  loading: false,
  saving: false,
  error: null,

  async fetchConfig() {
    const ws = useWebSocketStore.getState()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('email.config.get', null)
      set({ config: response, loading: false })
      return response
    } catch (error) {
      set({ loading: false, error: errorMessage(error, 'Unable to fetch operator email config') })
      throw error
    }
  },

  async updateConfig(payload) {
    const ws = useWebSocketStore.getState()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('email.config.update', payload)
      set({ config: response, saving: false })
      return response
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to update operator email config') })
      throw error
    }
  },

  async setProviderSecret(payload) {
    const ws = useWebSocketStore.getState()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('email.provider.secret.set', payload)
      const current = get().config
      set({ config: current ? { ...current, secrets: response.secrets } : current, saving: false })
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to store provider secret') })
      throw error
    }
  },

  async fetchSesAccessKey() {
    const ws = useWebSocketStore.getState()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('email.provider.secret.get', {
        provider: 'ses',
        include_secret_access_key: false,
      })
      const current = get().config
      set({ config: current ? { ...current, secrets: response.secrets } : current, saving: false })
      return response
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to fetch SES access key') })
      throw error
    }
  },

  async revealSesSecretAccessKey() {
    const ws = useWebSocketStore.getState()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('email.provider.secret.get', {
        provider: 'ses',
        include_secret_access_key: true,
      })
      const current = get().config
      set({ config: current ? { ...current, secrets: response.secrets } : current, saving: false })
      return response
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to reveal SES secret access key') })
      throw error
    }
  },

  async sendTest(payload) {
    const ws = useWebSocketStore.getState()
    set({ saving: true, error: null })
    try {
      const response = await ws.sendCommand('email.test.send', payload)
      set({ saving: false })
      await get().fetchHistory(25).catch(() => undefined)
      return response
    } catch (error) {
      set({ saving: false, error: errorMessage(error, 'Unable to send test email') })
      throw error
    }
  },

  async fetchHistory(limit = 25) {
    const ws = useWebSocketStore.getState()
    try {
      const response = await ws.sendCommand('email.history', { limit })
      set({ history: response.history })
    } catch (error) {
      set({ error: errorMessage(error, 'Unable to fetch email history') })
      throw error
    }
  },
}))
