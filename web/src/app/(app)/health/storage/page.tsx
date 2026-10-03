import type { Metadata } from 'next'
import { StoragePage } from '@/features/health/StoragePage'

export const metadata: Metadata = { title: 'Storage · Health' }

export default function Page() {
  return <StoragePage />
}
