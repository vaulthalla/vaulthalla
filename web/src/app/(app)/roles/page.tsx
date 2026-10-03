import { Suspense } from 'react'
import type { Metadata } from 'next'
import { RolesPage } from '@/features/access/roles/RolesPage'

export const metadata: Metadata = { title: 'Roles' }

export default function Page() {
  return (
    <Suspense>
      <RolesPage />
    </Suspense>
  )
}
