import type { Metadata } from 'next'
import { EditRolePage } from '@/features/access/roles/RoleEditor'

export const metadata: Metadata = { title: 'Role' }

export default function Page() {
  return <EditRolePage />
}
