import type { Metadata } from 'next'
import { SyncConflictsPage } from '@/features/syncConflicts/SyncConflictsPage'

export const metadata: Metadata = { title: 'Sync Conflicts' }

export default function Page() {
  return <SyncConflictsPage />
}
