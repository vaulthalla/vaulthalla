import type { Metadata } from 'next'
import { VaultSettings } from '@/features/vaults/settings/VaultSettings'

export const metadata: Metadata = { title: 'Vault settings' }

export default function Page() {
  return <VaultSettings />
}
