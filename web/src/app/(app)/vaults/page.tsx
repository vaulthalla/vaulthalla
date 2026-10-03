import type { Metadata } from 'next'
import { VaultsPage } from '@/features/vaults/VaultsPage'

export const metadata: Metadata = { title: 'Vaults' }

export default function Page() {
  return <VaultsPage />
}
