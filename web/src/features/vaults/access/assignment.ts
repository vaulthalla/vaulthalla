import type { VaultRoleDTO } from '@/models/permission'
import type { AssignmentSubject } from '@/features/vaults/model'

export interface AssignmentRow {
  subject: AssignmentSubject
  name: string | null
  role: VaultRoleDTO
}

export const subjectLabel = (row: Pick<AssignmentRow, 'subject' | 'name'>) =>
  row.name ?? `${row.subject.type === 'user' ? 'User' : 'Group'} #${row.subject.id}`
