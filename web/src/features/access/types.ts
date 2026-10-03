// Wire shapes for identities, groups and roles, exactly as core serializes them (identities/User.cpp,
// identities/Group.cpp, rbac/role/{Meta,Admin,Vault}.cpp, rbac/permission/Permission.cpp). Timestamps are strings
// or unix numbers depending on the object; render them through lib/format, never `new Date()` directly.

export interface PermissionRecord {
  id?: number
  bit_position: number
  qualified: string
  slug: string
  description: string
  // Present on a role's permission list; absent on the permissions.list catalog.
  value?: boolean
}

export interface AdminRoleRecord {
  id: number
  name: string
  description: string
  created_at?: number | string | null
  updated_at?: number | string | null
  assigned_at?: number | string | null
  user_id?: number | null
  // Lists of users may carry a slim role (id and name only); only roles.admin.* and the session user carry these.
  permissions?: PermissionRecord[]
}

export interface VaultRoleAssignmentInfo {
  // core sends these ids as strings
  subject_id: string | number
  vault_id: string | number
  subject_type: 'user' | 'group' | string
}

export interface VaultRoleRecord {
  id: number
  name: string
  description: string
  created_at?: number | string | null
  updated_at?: number | string | null
  assigned_at?: number | string | null
  permissions?: PermissionRecord[]
  assignment?: VaultRoleAssignmentInfo
}

export interface UserRecord {
  id: number
  name: string
  email: string | null
  is_active: boolean
  is_protected?: boolean
  system_only?: boolean
  created_at?: string | null
  updated_at?: string | null
  last_login: string | null
  password_changed_at?: string | null
  deactivated_at?: string | null
  linux_uid?: number
  created_by?: number
  updated_by?: number
  admin_role?: AdminRoleRecord | null
  // An empty set is serialized as an object, a non-empty one as an array.
  vault_roles?: VaultRoleRecord[] | Record<string, never>
}

export interface PublicUserRecord {
  id: number
  name: string
  email: string | null
  is_active: boolean
}

export interface GroupMemberRecord {
  user: PublicUserRecord
  joined_at: string | null
}

export interface GroupRecord {
  id: number
  name: string
  description: string | null
  created_at?: string | null
  updated_at?: string | null
  gid?: number
  members: GroupMemberRecord[]
}

export const vaultRolesOf = (user: Pick<UserRecord, 'vault_roles'> | null | undefined): VaultRoleRecord[] =>
  Array.isArray(user?.vault_roles) ? user.vault_roles : []
