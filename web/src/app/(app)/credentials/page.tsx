import type { Metadata } from 'next'
import { CredentialsPage } from '@/features/credentials/CredentialsPage'

export const metadata: Metadata = { title: 'Provider credentials' }

export default function Page() {
  return <CredentialsPage />
}
