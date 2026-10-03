'use client'

import { create } from 'zustand'
import { WsClient } from '@/lib/ws/client'
import { errorMessage } from '@/lib/ws/errors'
import type { PublicShare, ShareStatus } from '@/models/linkShare'

interface ShareSessionState {
  status: ShareStatus
  publicToken: string | null
  sessionId: string | null
  sessionToken: string | null
  challengeId: string | null
  share: PublicShare | null
  error: string | null
}

export const useShareSession = create<ShareSessionState>(() => ({
  status: 'idle',
  publicToken: null,
  sessionId: null,
  sessionToken: null,
  challengeId: null,
  share: null,
  error: null,
}))

let openSeq = 0

// The anonymous recipient's socket. A reconnect lands on a fresh server session, so the first refusal after one
// re-opens the share (the share_refresh cookie restores an email-verified session) and retries once.
export const shareApi = new WsClient({
  path: '/ws/share',
  onUnauthorized: async () => {
    const token = useShareSession.getState().publicToken
    if (!token) return false
    await openShare(token)
    return useShareSession.getState().status === 'ready'
  },
})

export const openShare = async (publicToken: string) => {
  const seq = ++openSeq
  useShareSession.setState({ status: 'opening', publicToken, error: null })
  try {
    const response = await shareApi.send('share.session.open', { public_token: publicToken })
    if (seq !== openSeq) return
    useShareSession.setState({
      status: response.status,
      sessionId: response.session_id,
      sessionToken: response.session_token,
      share: response.share,
      error: null,
    })
  } catch (error) {
    if (seq !== openSeq) return
    const message = errorMessage(error, 'This share link could not be opened')
    const lower = message.toLowerCase()
    useShareSession.setState({
      status: lower.includes('revoked') ? 'revoked' : lower.includes('expired') ? 'expired' : 'error',
      error: message,
    })
  }
}

export const startEmailChallenge = async (email: string) => {
  const { publicToken, sessionToken } = useShareSession.getState()
  const response = await shareApi.send('share.email.challenge.start', {
    email,
    public_token: publicToken ?? undefined,
    session_token: sessionToken ?? undefined,
  })
  useShareSession.setState({
    status: 'email_required',
    sessionId: response.session_id,
    sessionToken: response.session_token ?? sessionToken,
    challengeId: response.challenge_id,
    share: response.share,
    error: null,
  })
}

export const confirmEmailChallenge = async (code: string) => {
  const { challengeId, sessionId, sessionToken } = useShareSession.getState()
  if (!challengeId || !sessionId || !sessionToken) throw new Error('Start the email check first')
  const response = await shareApi.send('share.email.challenge.confirm', {
    challenge_id: challengeId,
    session_id: sessionId,
    session_token: sessionToken,
    code,
  })
  useShareSession.setState({
    status: response.status,
    sessionId: response.session_id,
    challengeId: response.verified ? null : challengeId,
    error: null,
  })
}
