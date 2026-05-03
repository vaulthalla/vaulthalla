import { create } from 'zustand'
import { v4 as uuidv4 } from 'uuid'
import { WebSocketCommandMap } from '@/util/webSocketCommands'
import { getWebsocketUrl } from '@/util/getUrl'
import { subscribeWithSelector } from 'zustand/middleware'

interface ShareWebSocketMessage<C extends keyof WebSocketCommandMap> {
  command: C
  payload: WebSocketCommandMap[C]['payload']
  requestId: string
  token?: string
}

interface ShareWebSocketStore {
  socket: WebSocket | null
  connected: boolean
  pending: Record<string, SharePendingRequest>

  waitForConnection: () => Promise<void>
  connect: () => void
  disconnect: () => void
  sendCommand: <C extends keyof WebSocketCommandMap>(
    command: C,
    payload: WebSocketCommandMap[C]['payload'],
  ) => Promise<WebSocketCommandMap[C]['response']>
}

const isErrorStatus = (status: unknown) => status === 'ERROR' || status === 'UNAUTHORIZED' || status === 'INTERNAL_ERROR' || status === 'error'
const isBootstrapCommand = (command: keyof WebSocketCommandMap) =>
  command === 'share.session.open' || command === 'share.email.challenge.start' || command === 'share.email.challenge.confirm'
const commandTimeoutMs = (command: keyof WebSocketCommandMap) => {
  if (command === 'share.upload.finish' || command === 'fs.upload.finish') return 60000
  if (command === 'share.upload.start' || command === 'fs.upload.start' || command === 'share.preview.get') return 20000
  return 10000
}
const connectionTimeoutMs = 10000

interface SharePendingRequest {
  resolve: <C extends keyof WebSocketCommandMap>(
    data: WebSocketCommandMap[C]['response'] | PromiseLike<WebSocketCommandMap[C]['response']>,
  ) => void
  reject: (error: Error) => void
  timeout: ReturnType<typeof setTimeout>
}

export const useShareWebSocketStore = create<ShareWebSocketStore>()(
  subscribeWithSelector((set, get) => {
    let isConnecting = false

    return {
      socket: null,
      connected: false,
      pending: {},

      waitForConnection: async (): Promise<void> => {
        return new Promise((resolve, reject) => {
          if (get().connected) return resolve()
          if (!get().socket) return reject(new Error('Share WebSocket is not connected'))

          const cleanupSubscriptions: Array<() => void> = []
          let timeout: ReturnType<typeof setTimeout> | null = null
          let settled = false

          const finish = (error?: Error) => {
            if (settled) return
            settled = true
            if (timeout) clearTimeout(timeout)
            cleanupSubscriptions.forEach(cleanup => cleanup())
            if (error) reject(error)
            else resolve()
          }

          timeout = setTimeout(() => {
            finish(new Error('Share WebSocket connection timed out. Try opening the share again.'))
          }, connectionTimeoutMs)

          const unsubscribe = useShareWebSocketStore.subscribe(
            state => ({ connected: state.connected, socket: state.socket }),
            state => {
              if (state.connected) finish()
              else if (!state.socket) finish(new Error('Share WebSocket disconnected. Try opening the share again.'))
            },
          )
          cleanupSubscriptions.push(unsubscribe)
          const current = get()
          if (current.connected) finish()
          else if (!current.socket) finish(new Error('Share WebSocket disconnected. Try opening the share again.'))
        })
      },

      connect: () => {
        const { socket, connected } = get()
        if (connected || socket || isConnecting) return
        isConnecting = true

        const ws = new WebSocket(getWebsocketUrl('/ws/share'))

        ws.onopen = () => {
          set({ connected: true })
          isConnecting = false
        }

        ws.onerror = () => {
          console.warn('[ShareWS] WebSocket error event')
        }

        ws.onclose = event => {
          isConnecting = false
          if (get().socket !== ws) return

          const pending = get().pending
          console.warn(
            `[ShareWS] Disconnected with ${Object.keys(pending).length} pending request(s); code=${event.code}; clean=${event.wasClean}`,
          )
          Object.values(pending).forEach(request => {
            clearTimeout(request.timeout)
            request.reject(new Error('Share WebSocket disconnected. Reopen the share link and try again.'))
          })
          set({ connected: false, socket: null, pending: {} })
        }

        ws.onmessage = event => {
          try {
            const message = JSON.parse(event.data)
            const handler = get().pending[message.requestId]
            if (!handler) return

            const newPending = { ...get().pending }
            delete newPending[message.requestId]
            set({ pending: newPending })
            clearTimeout(handler.timeout)

            if (isErrorStatus(message.status)) {
              handler.reject(new Error(message.error || message.message || 'Share websocket command failed'))
            } else {
              handler.resolve(Promise.resolve(message.data ?? {}))
            }
          } catch (err) {
            console.error('[ShareWS] Failed to parse message', err)
          }
        }

        set({ socket: ws })
      },

      disconnect: () => {
        const socket = get().socket
        isConnecting = false
        const pending = get().pending
        Object.values(pending).forEach(request => {
          clearTimeout(request.timeout)
          request.reject(new Error('Share WebSocket disconnected. Reopen the share link and try again.'))
        })
        if (socket) socket.close()
        set({ socket: null, connected: false, pending: {} })
      },

      sendCommand: async (command, payload) => {
        let { socket, connected } = get()
        const { connect } = get()

        if (!connected || !socket) {
          if (!isBootstrapCommand(command))
            throw new Error('Share session disconnected. Reopen the share link and try again.')
          connect()
          await get().waitForConnection()
          ;({ socket, connected } = get())
        }

        if (!socket || !connected) throw new Error('Share WebSocket is not connected')

        const requestId = uuidv4()
        const message: ShareWebSocketMessage<typeof command> = { command, payload, requestId }

        return new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            reject(new Error('Share request timed out'))
            const updated = { ...get().pending }
            delete updated[requestId]
            set({ pending: updated })
          }, commandTimeoutMs(command))

          set(state => ({
            pending: {
              ...state.pending,
              [requestId]: { resolve, reject, timeout },
            },
          }))

          try {
            socket.send(JSON.stringify(message))
          } catch (error) {
            clearTimeout(timeout)
            const updated = { ...get().pending }
            delete updated[requestId]
            set({ pending: updated })
            reject(error instanceof Error ? error : new Error('Unable to send share websocket command'))
          }
        })
      },
    }
  }),
)
