import type { Metadata } from 'next'
import { NewCredentialPage } from '@/features/credentials/CredentialForm'

export const metadata: Metadata = { title: 'Add provider credential' }

export default function Page() {
  return <NewCredentialPage />
}
