import type { Metadata } from 'next'
import { FilesPage } from '@/features/files/FilesPage'

export const metadata: Metadata = { title: 'Files' }

export default function Page() {
  return <FilesPage />
}
