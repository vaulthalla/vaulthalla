import { AdminRole, VaultRole } from '@/models/role'
import { AdminRoleDTO, VaultRoleDTO } from '@/models/permission'

export interface IUser {
  id: number
  name: string
  email: string
  permissions: Record<string, boolean>
  created_at: string
  last_login: string
  is_active: boolean
  admin_role: AdminRoleDTO
  vault_roles: VaultRoleDTO[]
}

export class User {
  id: number
  name: string
  email: string
  permissions: Record<string, boolean>
  created_at: string
  last_login: string
  is_active: boolean
  admin_role: AdminRole
  vault_roles: VaultRole[]

  constructor(data: IUser) {
    this.id = data.id
    this.name = data.name
    this.email = data.email
    this.permissions = data.permissions || {}
    this.created_at = data.created_at
    this.last_login = data.last_login
    this.is_active = data.is_active
    this.admin_role = AdminRole.fromData(data.admin_role)
    this.vault_roles = data.vault_roles.map(role => VaultRole.fromData(role))
  }

  static fromJSON(data: IUser): User {
    return new User(data)
  }
}
