import type { Metadata } from 'next'
import { NewVaultPage } from '@/features/vaults/NewVaultPage'

export const metadata: Metadata = { title: 'New vault' }

export default function Page() {
  return <NewVaultPage />
}
