import { Suspense } from 'react'
import type { Metadata } from 'next'
import { NewRolePage } from '@/features/access/roles/RoleEditor'

export const metadata: Metadata = { title: 'New role' }

export default function Page() {
  return (
    <Suspense>
      <NewRolePage />
    </Suspense>
  )
}
