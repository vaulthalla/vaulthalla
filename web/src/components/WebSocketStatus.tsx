'use client'

import { useWebSocketStore } from '@/stores/useWebSocket'
import { useEffect } from 'react'

export const WebSocketStatus = () => {
  const connected = useWebSocketStore(state => state.connected)
  const connect = useWebSocketStore(state => state.connect)

  useEffect(() => {
    connect()
  }, [connect])

  return <p style={{ color: connected ? 'green' : 'red' }}>WebSocket is {connected ? 'connected' : 'not connected'}</p>
}
