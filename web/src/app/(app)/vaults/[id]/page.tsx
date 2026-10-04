import type { Metadata } from 'next'
import { VaultOverview } from '@/features/vaults/overview/VaultOverview'

export const metadata: Metadata = { title: 'Vault overview' }

export default function Page() {
  return <VaultOverview />
}
