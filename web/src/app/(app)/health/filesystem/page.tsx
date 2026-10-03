import type { Metadata } from 'next'
import { FilesystemPage } from '@/features/health/FilesystemPage'

export const metadata: Metadata = { title: 'Filesystem · Health' }

export default function Page() {
  return <FilesystemPage />
}
