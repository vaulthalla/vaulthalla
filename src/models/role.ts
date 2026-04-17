import {
  AdminRoleDTO,
  PermissionDTO,
  PermissionOverrideDTO,
  RoleType,
  SubjectType,
  VaultRoleDTO,
} from '@/models/permission'
import { toDate } from '@/util/toDate'

export class PermissionPayload {
  constructor(
    public qualified: string,
    public value: boolean,
  ) {}
}

export type AdminRolePayload = {
  id?: number
  type: 'admin'
  name: string
  description: string
  permissions: PermissionPayload[]
}
export type VaultRolePayload = {
  id?: number
  type: 'vault'
  name: string
  description: string
  permissions: PermissionPayload[]
  vault_id: number
  subject_type?: SubjectType | null
  subject_id?: number | null
  permission_overrides?: VaultPermissionOverridePayload[]
}

export type VaultPermissionOverridePayload = {
  permission_qualified: string
  enabled: boolean
  effect: 'allow' | 'deny'
  pattern: string
}

export class Permission {
  constructor(
    public bit_position: number,
    public qualified: string,
    public slug: string,
    public description: string,
    public value: boolean,
    public id?: number,
    public created_at?: Date | null,
    public updated_at?: Date | null,
  ) {}

  static fromData(data: PermissionDTO): Permission {
    return new Permission(
      data.bit_position,
      data.qualified,
      data.slug,
      data.description,
      data.value,
      data.id,
      toDate(data.created_at),
      toDate(data.updated_at),
    )
  }

  get segments(): string[] {
    return this.qualified.split('.')
  }

  get categoryPath(): string[] {
    return this.segments.slice(0, -1)
  }

  get displayName(): string {
    return this.slug.replace(/[-_]/g, ' ').replace(/\b\w/g, c => c.toUpperCase())
  }
}

export class PermissionOverride {
  constructor(
    public permission: Permission,
    public enabled: boolean,
    public regex: string,
  ) {}

  static fromData(data: PermissionOverrideDTO): PermissionOverride {
    return new PermissionOverride(Permission.fromData(data.permission), data.enabled, data.regex)
  }
}

export abstract class Role {
  constructor(
    public id: number,
    public type: RoleType,
    public name: string,
    public description: string,
    public created_at: Date | null,
    public updated_at: Date | null,
    public permissions: Permission[],
  ) {}
}

export class AdminRole extends Role {
  constructor(
    id: number,
    name: string,
    description: string,
    created_at: Date | null,
    updated_at: Date | null,
    permissions: Permission[],
    public assignment_id: number | null = null,
    public user_id: number | null = null,
    public assigned_at: Date | null = null,
  ) {
    super(id, 'admin', name, description, created_at, updated_at, permissions)
  }

  static fromData(data: AdminRoleDTO): AdminRole {
    return new AdminRole(
      data.id,
      data.name,
      data.description,
      toDate(data.created_at),
      toDate(data.updated_at),
      (data.permissions ?? []).map(Permission.fromData),
      data.assignment_id ?? null,
      data.user_id ?? null,
      toDate(data.assigned_at),
    )
  }
}

export class VaultRole extends Role {
  constructor(
    id: number,
    name: string,
    description: string,
    created_at: Date | null,
    updated_at: Date | null,
    permissions: Permission[],
    public vault_id: number,
    public assignment_id: number | null = null,
    public subject_type: SubjectType | null = null,
    public subject_id: number | null = null,
    public assigned_at: Date | null = null,
    public permission_overrides: PermissionOverride[] = [],
  ) {
    super(id, 'vault', name, description, created_at, updated_at, permissions)
  }

  static fromData(data: VaultRoleDTO): VaultRole {
    return new VaultRole(
      data.id,
      data.name,
      data.description,
      toDate(data.created_at),
      toDate(data.updated_at),
      (data.permissions ?? []).map(Permission.fromData),
      data.vault_id,
      data.assignment_id ?? null,
      data.subject_type ?? null,
      data.subject_id ?? null,
      toDate(data.assigned_at),
      (data.permission_overrides ?? []).map(PermissionOverride.fromData),
    )
  }
}
