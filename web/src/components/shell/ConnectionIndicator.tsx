'use client'

import { useSyncExternalStore } from 'react'
import { api } from '@/lib/session'
import { Badge } from '@/components/ui/Badge'

export const useConnectionStatus = () => useSyncExternalStore(api.subscribe, api.getStatus, () => 'idle' as const)

// Silent while connected; says so plainly while the daemon is unreachable.
export const ConnectionIndicator = () => {
  const status = useConnectionStatus()
  if (status === 'open' || status === 'idle' || status === 'connecting') return null
  return (
    <span role="status">
      <Badge tone="warn" dot pulse>
        {status === 'reconnecting' ? 'Reconnecting…' : 'Offline'}
      </Badge>
    </span>
  )
}
