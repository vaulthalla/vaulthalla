import type { Metadata } from 'next'
import { VaultSync } from '@/features/vaults/sync/VaultSync'

export const metadata: Metadata = { title: 'Vault sync & cost' }

export default function Page() {
  return <VaultSync />
}
