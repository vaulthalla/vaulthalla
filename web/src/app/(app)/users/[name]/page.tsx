import type { Metadata } from 'next'
import { UserDetailPage } from '@/features/access/users/UserDetailPage'

export async function generateMetadata({ params }: { params: Promise<{ name: string }> }): Promise<Metadata> {
  const { name } = await params
  return { title: decodeURIComponent(name) }
}

export default function Page() {
  return <UserDetailPage />
}
