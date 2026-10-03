import type { Metadata } from 'next'
import { NewUserPage } from '@/features/access/users/NewUserPage'

export const metadata: Metadata = { title: 'New user' }

export default function Page() {
  return <NewUserPage />
}
