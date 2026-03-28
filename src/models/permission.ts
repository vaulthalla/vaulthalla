export type RoleType = 'admin' | 'vault'
export type SubjectType = 'user' | 'group'

export interface PermissionDTO {
  id?: number
  bit_position: number
  qualified: string
  slug: string
  description: string
  value: boolean
  created_at?: number | string | null
  updated_at?: number | string | null
}

export interface PermissionOverrideDTO {
  permission: PermissionDTO
  enabled: boolean
  regex: string
}

export interface RoleDTO {
  id: number
  type: RoleType
  name: string
  description: string
  created_at: number | string | null
  updated_at?: number | string | null
}

export interface AdminRoleDTO extends RoleDTO {
  type: 'admin'
  assignment_id?: number | null
  user_id?: number | null
  assigned_at?: number | string | null
  permissions: PermissionDTO[]
}

export interface VaultRoleDTO extends RoleDTO {
  type: 'vault'
  assignment_id?: number | null
  vault_id: number
  subject_type?: SubjectType | null
  subject_id?: number | null
  assigned_at?: number | string | null
  permission_overrides?: PermissionOverrideDTO[]
  permissions: PermissionDTO[]
}
