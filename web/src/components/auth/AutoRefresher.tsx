'use client'

import { useEffect } from 'react'
import { useAuthStore } from '@/stores/authStore'
import { useWebSocketStore } from '@/stores/useWebSocket'

export const AuthRefresher = () => {
  const token = useAuthStore(state => state.token)
  const refreshToken = useAuthStore(state => state.refreshToken)
  const connected = useWebSocketStore(state => state.connected)

  useEffect(() => {
    if (!token || !connected) return

    console.log('[AuthRefresher] Starting token refresh loop')
    const interval = setInterval(
      () => {
        refreshToken()
      },
      4 * 60 * 1000,
    ) // every 4 minutes

    return () => {
      clearInterval(interval)
      console.log('[AuthRefresher] Cleared token refresh loop')
    }
  }, [token, connected, refreshToken])

  return null
}
