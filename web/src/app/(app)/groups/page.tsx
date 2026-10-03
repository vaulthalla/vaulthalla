import { Suspense } from 'react'
import type { Metadata } from 'next'
import { GroupsPage } from '@/features/access/groups/GroupsPage'

export const metadata: Metadata = { title: 'Groups' }

export default function Page() {
  return (
    <Suspense>
      <GroupsPage />
    </Suspense>
  )
}
