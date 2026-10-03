import type { Metadata } from 'next'
import { OverviewPage } from '@/features/health/OverviewPage'

export const metadata: Metadata = { title: 'Health' }

export default function Page() {
  return <OverviewPage />
}
