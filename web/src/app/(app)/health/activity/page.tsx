import type { Metadata } from 'next'
import { ActivityPage } from '@/features/health/ActivityPage'

export const metadata: Metadata = { title: 'Activity · Health' }

export default function Page() {
  return <ActivityPage />
}
