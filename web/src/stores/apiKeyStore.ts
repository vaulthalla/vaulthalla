import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { APIKey, S3APIKey } from '@/models/apiKey'

interface ApiKeyStore {
  apiKeys: APIKey[]
  fetchApiKeys: (payload: WSCommandPayload<'storage.apiKey.list.user'>) => Promise<void>
  addApiKey: (payload: WSCommandPayload<'storage.apiKey.add'>) => Promise<void>
  removeApiKey: (payload: WSCommandPayload<'storage.apiKey.remove'>) => Promise<void>
  getApiKey: (payload: WSCommandPayload<'storage.apiKey.get'>) => Promise<APIKey | S3APIKey>
}

export const useApiKeyStore = create<ApiKeyStore>()(
  persist(
    (set, get) => ({
      apiKeys: [],

      async fetchApiKeys() {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('storage.apiKey.list', null)
        set({ apiKeys: JSON.parse(response.keys) })
      },

      async addApiKey(apiKeyPayload) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('storage.apiKey.add', apiKeyPayload)
        await get().fetchApiKeys(null)
      },

      async removeApiKey({ id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('storage.apiKey.remove', { id })

        const current = get().apiKeys.find(k => k.api_key_id === id)
        if (current) await get().fetchApiKeys(null)
      },

      async getApiKey({ id }) {
        const waitForConnection = useWebSocketStore.getState().waitForConnection
        await waitForConnection()

        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('storage.apiKey.get', { id })
        return response.api_key
      },
    }),
    {
      name: 'vaulthalla-api-keys',
      partialize: state => ({ apiKeys: state.apiKeys }),

      onRehydrateStorage: () => () => {
        console.log('[ApiKeyStore] Rehydrated from storage')
        ;(async () => {
          try {
            console.log('[ApiKeyStore] Waiting for WebSocket connection...')
            await (await import('@/stores/useWebSocket')).useWebSocketStore.getState().waitForConnection()
            console.log('[ApiKeyStore] WebSocket connected. Re-fetching API keys...')

            await useApiKeyStore.getState().fetchApiKeys(null)
            console.log('[ApiKeyStore] API key fetch complete')
          } catch (err) {
            console.error('[ApiKeyStore] Rehydrate fetch failed:', err)
          }
        })()
      },
    },
  ),
)
