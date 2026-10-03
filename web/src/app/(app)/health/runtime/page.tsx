import type { Metadata } from 'next'
import { RuntimePage } from '@/features/health/RuntimePage'

export const metadata: Metadata = { title: 'Runtime · Health' }

export default function Page() {
  return <RuntimePage />
}
