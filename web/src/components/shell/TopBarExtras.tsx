'use client'

import dynamic from 'next/dynamic'
import { TransfersButton } from '@/features/files/TransfersButton'

// Status indicators load after first paint; each renders nothing for sessions it doesn't apply to.
const HealthIndicator = dynamic(() => import('@/features/health/HealthIndicator').then(m => m.HealthIndicator), { ssr: false })
const SyncConflictsButton = dynamic(() => import('@/features/syncConflicts/SyncConflictsButton').then(m => m.SyncConflictsButton), {
  ssr: false,
})
const NotificationsBell = dynamic(() => import('@/features/cost/NotificationsBell').then(m => m.NotificationsBell), { ssr: false })

export const TopBarExtras = () => (
  <>
    <HealthIndicator />
    <SyncConflictsButton />
    <NotificationsBell />
    <TransfersButton />
  </>
)
