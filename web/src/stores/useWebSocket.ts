import { create } from 'zustand'
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
  pending: Record<
    string,
    <C extends keyof WebSocketCommandMap>(
      data: WebSocketCommandMap[C]['response'] | PromiseLike<WebSocketCommandMap[C]['response']>,
    ) => void
  >

  waitForConnection: () => Promise<void>
  connect: () => void
  disconnect: () => void
  sendCommand: <C extends keyof WebSocketCommandMap>(
    command: C,
    payload: WebSocketCommandMap[C]['payload'],
  ) => Promise<WebSocketCommandMap[C]['response']>
}

export const useWebSocketStore = create<WebSocketStore>()(
  subscribeWithSelector((set, get) => {
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

          const unsubscribe = useWebSocketStore.subscribe(
            state => state.connected,
            connected => {
              if (connected) {
                unsubscribe()
                resolve()
              }
            },
          )
        })
      },

      connect: () => {
        const { socket, connected } = get()

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
          set({ connected: false, socket: null })
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

            if (message.command === 'error' && message.status === 'unauthorized') {
              console.warn('[WS] Received unauthorized response — attempting token refresh')

              try {
                await useAuthStore.getState().refreshToken()
                console.log('[WS] Token refreshed — retrying original request not implemented yet')
              } catch (err) {
                console.error('[WS] Failed to refresh token:', err)
                useAuthStore.getState().logout()
              }

              return
            }

            const handler = get().pending[message.requestId]
            if (handler) {
              const newPending = { ...get().pending }
              delete newPending[message.requestId]
              set({ pending: newPending })

              if (message.status === 'error') {
                handler(Promise.reject(new Error(message.error || 'WebSocket command failed')))
              } else {
                handler(Promise.resolve(message.data ?? {}))
              }
            } else {
              if (message.status === 'error') {
                if (message.error === 'unauthorized') {
                  console.warn('[WS] Unauthorized request received, refreshing token...')
                  await useAuthStore.getState().refreshToken()
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
        if (socket) socket.close()
        set({ socket: null, connected: false, pending: {} })
      },

      sendCommand: async (command, payload) => {
        let { socket, connected } = get()
        const { connect } = get()
        const token = useAuthStore.getState().token

        if (!connected || !socket) {
          console.warn('[WS] Not connected, attempting to reconnect...')
          connect()
          await get().waitForConnection()
          ;({ socket, connected } = get())
        }

        if (!token && !command.startsWith('auth')) throw new Error('No auth token')

        if (!socket || !connected) throw new Error('WebSocket is not connected')

        const requestId = uuidv4()
        const message: WebSocketMessage<typeof command> = { command, payload, requestId, token: token || '' }

        return new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            reject(new Error('Request timed out'))
            const updated = { ...get().pending }
            delete updated[requestId]
            set({ pending: updated })
          }, 10000)

          set(state => ({
            pending: {
              ...state.pending,
              [requestId]: dataPromise => {
                clearTimeout(timeout)
                Promise.resolve(dataPromise).then(resolve).catch(reject)
              },
            },
          }))

          socket.send(JSON.stringify(message))
        })
      },
    }
  }),
)
