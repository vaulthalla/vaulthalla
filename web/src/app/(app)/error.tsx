'use client'

import { ErrorState } from '@/components/ui/State'

export default function Error({ error, reset }: { error: Error; reset: () => void }) {
  return <ErrorState error={error} onRetry={reset} />
}
