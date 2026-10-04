import type { Metadata } from 'next'
import { VaultGateway } from '@/features/vaults/VaultGateway'

export const metadata: Metadata = { title: 'Vault S3 gateway' }

export default function Page() {
  return <VaultGateway />
}
