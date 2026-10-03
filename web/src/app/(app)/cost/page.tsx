import React, { Suspense } from 'react'
import type { Metadata } from 'next'
import { CostPage } from '@/features/cost/CostPage'

export const metadata: Metadata = { title: 'Cost control' }

export default function Page() {
  return (
    <Suspense>
      <CostPage />
    </Suspense>
  )
}
