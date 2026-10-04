import type { Metadata } from 'next'
import { VaultAccess } from '@/features/vaults/access/VaultAccess'

export const metadata: Metadata = { title: 'Vault access' }

export default function Page() {
  return <VaultAccess />
}
