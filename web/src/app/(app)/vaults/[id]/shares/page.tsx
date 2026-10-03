import type { Metadata } from 'next'
import { VaultShares } from '@/features/vaults/VaultShares'

export const metadata: Metadata = { title: 'Vault shares' }

export default function Page() {
  return <VaultShares />
}
