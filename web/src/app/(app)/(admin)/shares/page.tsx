import type { Metadata } from 'next'
import SharesClientPage from './page.client'

export const metadata: Metadata = { title: 'Vaulthalla | Shares', description: 'Manage link shares' }

export default function SharesPage() {
  return <SharesClientPage />
}
