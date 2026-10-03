import type { Metadata } from 'next'
import { UsersPage } from '@/features/access/users/UsersPage'

export const metadata: Metadata = { title: 'Users' }

export default function Page() {
  return <UsersPage />
}
