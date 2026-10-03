'use client'

import { TransfersButton } from '@/features/files/TransfersButton'
import { HealthIndicator } from '@/features/health/HealthIndicator'

// Feature indicators that live in the top bar.
export const TopBarExtras = () => (
  <>
    <HealthIndicator />
    <TransfersButton />
  </>
)
