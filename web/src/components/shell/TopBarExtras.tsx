'use client'

import { TransfersButton } from '@/features/files/TransfersButton'
import { HealthIndicator } from '@/features/health/HealthIndicator'
import { NotificationsBell } from '@/features/cost/NotificationsBell'

// Feature indicators that live in the top bar. Each renders nothing for sessions it doesn't apply to.
export const TopBarExtras = () => (
  <>
    <HealthIndicator />
    <NotificationsBell />
    <TransfersButton />
  </>
)
