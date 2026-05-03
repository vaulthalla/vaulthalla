import { create } from 'zustand'
import { PublicShare, ShareStatus } from '@/models/linkShare'
import { useShareWebSocketStore } from '@/stores/useShareWebSocket'

interface VaultShareStore {
  status: ShareStatus
  publicToken: string | null
  sessionId: string | null
  sessionToken: string | null
  challengeId: string | null
  share: PublicShare | null
  error: string | null
  emailSubmitting: boolean
  codeSubmitting: boolean
  challengeStartedAt: number | null

  openSession: (publicToken: string) => Promise<void>
  startEmailChallenge: (email: string) => Promise<void>
  confirmEmailChallenge: (code: string) => Promise<void>
  clearShareSession: () => void
}

let shareOpenRequestId = 0

export const useVaultShareStore = create<VaultShareStore>()((set, get) => ({
  status: 'idle',
  publicToken: null,
  sessionId: null,
  sessionToken: null,
  challengeId: null,
  share: null,
  error: null,
  emailSubmitting: false,
  codeSubmitting: false,
  challengeStartedAt: null,

  async openSession(publicToken) {
    const requestId = ++shareOpenRequestId
    const current = get()
    const ws = useShareWebSocketStore.getState()
    const socketReady = ws.connected && ws.socket?.readyState === WebSocket.OPEN

    if (current.publicToken === publicToken && current.status === 'ready' && current.share && socketReady) return

    if ((current.publicToken && current.publicToken !== publicToken) || (current.publicToken === publicToken && current.status !== 'idle' && !socketReady)) {
      ws.disconnect()
    }

    set({
      status: 'opening',
      publicToken,
      sessionId: null,
      sessionToken: null,
      challengeId: null,
      share: null,
      error: null,
      emailSubmitting: false,
      codeSubmitting: false,
      challengeStartedAt: null,
    })

    try {
      ws.connect()
      await ws.waitForConnection()
      const response = await ws.sendCommand('share.session.open', { public_token: publicToken })
      if (get().publicToken !== publicToken || requestId !== shareOpenRequestId) return
      set({
        status: response.status,
        sessionId: response.session_id,
        sessionToken: response.session_token,
        share: response.share,
        error: null,
      })
    } catch (error) {
      if (get().publicToken !== publicToken || requestId !== shareOpenRequestId) throw error
      set({
        status: 'error',
        error: error instanceof Error ? error.message : 'Unable to open share',
      })
      throw error
    }
  },

  async startEmailChallenge(email) {
    const { publicToken, sessionToken } = get()
    const ws = useShareWebSocketStore.getState()
    if (!publicToken && !sessionToken) throw new Error('No share token is available')

    set({ emailSubmitting: true, error: null })

    try {
      const response = await ws.sendCommand('share.email.challenge.start', {
        email,
        public_token: publicToken ?? undefined,
        session_token: sessionToken ?? undefined,
      })
      set({
        status: 'email_required',
        sessionId: response.session_id,
        sessionToken: response.session_token ?? sessionToken,
        challengeId: response.challenge_id,
        share: response.share,
        error: null,
        emailSubmitting: false,
        challengeStartedAt: Date.now(),
      })
    } catch (error) {
      set({
        status: 'email_required',
        emailSubmitting: false,
        error: error instanceof Error ? error.message : 'Unable to start email challenge',
      })
      throw error
    }
  },

  async confirmEmailChallenge(code) {
    const { challengeId, sessionId, sessionToken } = get()
    if (!challengeId) throw new Error('No email challenge is active')
    if (!sessionId || !sessionToken) throw new Error('No share session is active')

    const ws = useShareWebSocketStore.getState()
    set({ codeSubmitting: true, error: null })
    try {
      const response = await ws.sendCommand('share.email.challenge.confirm', {
        challenge_id: challengeId,
        session_id: sessionId,
        session_token: sessionToken,
        code,
      })
      set({
        status: response.status,
        sessionId: response.session_id,
        challengeId: response.verified ? null : challengeId,
        error: null,
        codeSubmitting: false,
      })
    } catch (error) {
      set({
        status: 'email_required',
        codeSubmitting: false,
        error: error instanceof Error ? error.message : 'Unable to confirm email challenge',
      })
      throw error
    }
  },

  clearShareSession() {
    shareOpenRequestId += 1
    useShareWebSocketStore.getState().disconnect()
    set({
      status: 'idle',
      publicToken: null,
      sessionId: null,
      sessionToken: null,
      challengeId: null,
      share: null,
      error: null,
      emailSubmitting: false,
      codeSubmitting: false,
      challengeStartedAt: null,
    })
  },
}))
