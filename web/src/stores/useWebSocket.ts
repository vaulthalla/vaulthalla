import { create, type StoreApi, type UseBoundStore } from 'zustand'
import { v4 as uuidv4 } from 'uuid'
import { useAuthStore } from './authStore'
import { WebSocketCommandMap } from '@/util/webSocketCommands'
import { getWebsocketUrl } from '@/util/getUrl'
import { subscribeWithSelector } from 'zustand/middleware'

interface WebSocketMessage<C extends keyof WebSocketCommandMap> {
  command: C
  payload: WebSocketCommandMap[C]['payload']
  requestId: string
  token: string
}

interface WebSocketStore {
  socket: WebSocket | null
  connected: boolean
  pending: Record<string, PendingRequest>

  waitForConnection: () => Promise<void>
  connect: () => void
  disconnect: () => void
  sendCommand: <C extends keyof WebSocketCommandMap>(
    command: C,
    payload: WebSocketCommandMap[C]['payload'],
  ) => Promise<WebSocketCommandMap[C]['response']>
}

interface PendingRequest {
  resolve: (data: unknown) => void
  reject: (error: Error) => void
  timeout: ReturnType<typeof setTimeout>
}

const isAuthCommand = (command: keyof WebSocketCommandMap) => command.startsWith('auth.')
const isUnauthorizedMessage = (message: unknown) => {
  if (!message || typeof message !== 'object') return false
  const record = message as Record<string, unknown>
  return record.status === 'unauthorized' || record.status === 'UNAUTHORIZED' || record.error === 'unauthorized'
}
const isErrorMessage = (message: unknown) => {
  if (!message || typeof message !== 'object') return false
  const record = message as Record<string, unknown>
  return record.status === 'error' || record.status === 'ERROR' || record.status === 'INTERNAL_ERROR' || isUnauthorizedMessage(message)
}
const isUnauthorizedError = (error: unknown) => {
  if (!(error instanceof Error)) return false
  return error.message.toLowerCase().includes('unauthorized')
}

export const useWebSocketStore: UseBoundStore<StoreApi<WebSocketStore>> = create<WebSocketStore>()(
  subscribeWithSelector<WebSocketStore>((set, get) => {
    let reconnectTimeout: ReturnType<typeof setTimeout> | null = null
    let shouldReconnect = true
    let isConnecting = false

    return {
      socket: null,
      connected: false,
      pending: {},

      waitForConnection: async (): Promise<void> => {
        return new Promise(resolve => {
          if (get().connected) return resolve()

          const unsubscribe = useWebSocketStore.subscribe(state => {
            if (state.connected) {
              unsubscribe()
              resolve()
            }
          })
        })
      },

      connect: () => {
        const { socket, connected } = get()
        shouldReconnect = true

        if (reconnectTimeout) {
          clearTimeout(reconnectTimeout)
          reconnectTimeout = null
        }

        if (connected || socket || isConnecting) return
        isConnecting = true

        console.log('[WS] Attempting to connect...')
        const ws = new WebSocket(getWebsocketUrl())

        ws.onopen = () => {
          set({ connected: true })
          isConnecting = false
          console.log('[WS] Connected')
        }

        ws.onclose = () => {
          isConnecting = false
          const pending = get().pending
          Object.values(pending).forEach(request => {
            clearTimeout(request.timeout)
            request.reject(new Error('WebSocket disconnected'))
          })
          set({ connected: false, socket: null, pending: {} })
          console.warn('[WS] Disconnected')

          if (shouldReconnect) {
            const retryDelay = 2000 // ms, can implement exponential backoff
            console.log(`[WS] Attempting to reconnect in ${retryDelay / 1000}s...`)
            reconnectTimeout = setTimeout(() => {
              get().connect()
            }, retryDelay)
          }
        }

        ws.onmessage = async event => {
          try {
            const message = JSON.parse(event.data)

            if (message.token) useAuthStore.getState().setToken(message.token)

            if (message.command === 'error' && isUnauthorizedMessage(message)) {
              console.warn('[WS] Received unauthorized response — attempting token refresh')

              await useAuthStore
                .getState()
                .refreshToken()
                .catch(() => undefined)

              return
            }

            const handler = get().pending[message.requestId]
            if (handler) {
              const newPending = { ...get().pending }
              delete newPending[message.requestId]
              set({ pending: newPending })
              clearTimeout(handler.timeout)

              if (isErrorMessage(message)) {
                const error = isUnauthorizedMessage(message) ? 'unauthorized' : message.error || 'WebSocket command failed'
                handler.reject(new Error(error))
              } else {
                handler.resolve(message.data ?? {})
              }
            } else {
              if (isErrorMessage(message)) {
                if (isUnauthorizedMessage(message)) {
                  console.warn('[WS] Unauthorized request received, refreshing token...')
                  await useAuthStore
                    .getState()
                    .refreshToken()
                    .catch(() => undefined)
                } else console.warn('[WS] Error received without handler:', message.error)
              }
            }
          } catch (err) {
            console.error('[WS] Failed to parse message', err)
          }
        }

        set({ socket: ws })
      },

      disconnect: () => {
        const socket = get().socket
        shouldReconnect = false
        if (reconnectTimeout) clearTimeout(reconnectTimeout)
        Object.values(get().pending).forEach(request => {
          clearTimeout(request.timeout)
          request.reject(new Error('WebSocket disconnected'))
        })
        if (socket) socket.close()
        set({ socket: null, connected: false, pending: {} })
      },

      sendCommand: async (command, payload) => {
        const ensureConnection = async () => {
          let { socket, connected } = get()
          const { connect } = get()

          if (!connected || !socket) {
            console.warn('[WS] Not connected, attempting to reconnect...')
            connect()
            await get().waitForConnection()
            ;({ socket, connected } = get())
          }

          if (!socket || !connected) throw new Error('WebSocket is not connected')

          return socket
        }

        const sendOnce = async (token: string | null) => {
          const socket = await ensureConnection()

          if (!token && !isAuthCommand(command)) throw new Error('No auth token')

          const requestId = uuidv4()
          const message: WebSocketMessage<typeof command> = { command, payload, requestId, token: token || '' }

          return new Promise<WebSocketCommandMap[typeof command]['response']>((resolve, reject) => {
            const timeout = setTimeout(() => {
              reject(new Error('Request timed out'))
              const updated = { ...get().pending }
              delete updated[requestId]
              set({ pending: updated })
            }, 10000)

            set(state => ({
              pending: {
                ...state.pending,
                [requestId]: {
                  timeout,
                  resolve: data => resolve(data as WebSocketCommandMap[typeof command]['response']),
                  reject,
                },
              },
            }))

            try {
              socket.send(JSON.stringify(message))
            } catch (error) {
              clearTimeout(timeout)
              const updated = { ...get().pending }
              delete updated[requestId]
              set({ pending: updated })
              reject(error instanceof Error ? error : new Error('Unable to send websocket command'))
            }
          })
        }

        if (isAuthCommand(command)) return sendOnce(useAuthStore.getState().token)

        let token = useAuthStore.getState().token
        if (!token) {
          await useAuthStore.getState().refreshToken()
          token = useAuthStore.getState().token
        }

        try {
          return await sendOnce(token)
        } catch (error) {
          if (!isUnauthorizedError(error)) throw error
          await useAuthStore.getState().refreshToken()
          return sendOnce(useAuthStore.getState().token)
        }
      },
    }
  }),
)
