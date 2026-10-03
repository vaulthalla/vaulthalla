import type { Metadata } from 'next'
import { notFound } from 'next/navigation'
import { EditCredentialPage } from '@/features/credentials/CredentialForm'

export const metadata: Metadata = { title: 'Provider credential' }

export default async function Page({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params
  const numeric = Number(id)
  if (!Number.isInteger(numeric) || numeric <= 0) notFound()
  return <EditCredentialPage id={numeric} />
}
