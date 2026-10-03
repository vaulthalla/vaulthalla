import React, { Suspense } from 'react'
import type { Metadata } from 'next'
import { GatewayPage } from '@/features/gateway/GatewayPage'

export const metadata: Metadata = { title: 'S3 gateway' }

export default function Page() {
  return (
    <Suspense>
      <GatewayPage />
    </Suspense>
  )
}
