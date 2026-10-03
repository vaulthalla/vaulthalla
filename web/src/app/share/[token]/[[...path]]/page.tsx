import type { Metadata } from 'next'
import { SharePage } from '@/features/share/SharePage'

export const metadata: Metadata = { title: 'Shared files', robots: { index: false, follow: false } }

export default async function Page({ params }: { params: Promise<{ token: string }> }) {
  const { token } = await params
  return <SharePage token={decodeURIComponent(token)} />
}
