'use client'

import { useEffect } from 'react'
import { useWebSocketStore } from '@/stores/useWebSocket'

export const useWebSocketLifecycle = () => {
  const { connect, disconnect } = useWebSocketStore()

  useEffect(() => {
    const id = setTimeout(() => connect(), 1)

    return () => {
      clearTimeout(id)
      disconnect()
    }
  }, [connect, disconnect])
}
