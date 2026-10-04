import type { Metadata } from 'next'
import { SharesPage } from '@/features/shares/SharesPage'

export const metadata: Metadata = { title: 'Shares' }

export default function Page() {
  return <SharesPage />
}
