'use client'

import React from 'react'
import { useWebSocketLifecycle } from '@/hooks/useWebSocketLifecycle'

export const WebSocketProvider = ({ children }: { children: React.ReactNode }) => {
  useWebSocketLifecycle()
  return children
}
