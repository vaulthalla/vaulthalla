import { PriceBudgetPolicy, PriceBudgetPolicyPayload, PriceBudgetStatus } from '@/models/pricing/priceBudget'
import { PriceBudgetLedgerEntry } from '@/models/pricing/priceBudgetLedger'
import { Permission } from '@/models/role'
import type { PermissionDTO } from '@/models/permission'
import {
  asBoolean,
  asDateValue,
  asNullableNumber,
  asNullableString,
  asNumber,
  asString,
  isRecord,
} from '@/models/pricing/common'

export type S3GatewayCredentialScopeMode = 'user_access' | 'global' | 'vault_allowlist'
export type S3GatewayBucketMode = 'local' | 'remote_cache' | 'remote_proxy' | string
export type S3GatewayCredentialVaultRoleOverrideEffect = 'allow' | 'deny'

export interface S3GatewayVaultRef {
  id: number
  name: string
  owner_id?: number | null
}

export interface S3GatewayVaultRoleRef {
  id: number
  name: string
  description: string
  permissions?: Permission[]
}

export function asGatewayScopeMode(value: unknown): S3GatewayCredentialScopeMode {
  if (typeof value === 'string') {
    const normalized = value.toLowerCase().replaceAll('-', '_')
    if (normalized === 'global' || normalized === 'vault_allowlist') return normalized
  }
  return 'user_access'
}

export class S3GatewayStatus {
  running = false
  configured = false
  ready = false
  host = ''
  port = 0
  endpoint = ''
  active_sessions = 0
  total_requests = 0
  failed_requests = 0

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.running = asBoolean(data.running)
    this.configured = asBoolean(data.configured)
    this.ready = asBoolean(data.ready)
    this.host = asString(data.host)
    this.port = asNumber(data.port)
    this.endpoint = asString(data.endpoint)
    this.active_sessions = asNumber(data.active_sessions)
    this.total_requests = asNumber(data.total_requests)
    this.failed_requests = asNumber(data.failed_requests)
  }

  static from(input: unknown): S3GatewayStatus {
    return new S3GatewayStatus(input)
  }
}

export class S3GatewayCredentialVaultScope {
  credential_id = 0
  vault_id = 0
  can_list = true
  can_read = true
  can_write = false
  can_delete = false
  can_admin = false
  role: { id: number; name: string; description: string } | null = null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.credential_id = asNumber(data.credential_id)
    this.vault_id = asNumber(data.vault_id)
    this.can_list = asBoolean(data.can_list, true)
    this.can_read = asBoolean(data.can_read, true)
    this.can_write = asBoolean(data.can_write)
    this.can_delete = asBoolean(data.can_delete)
    this.can_admin = asBoolean(data.can_admin)
    this.role = isRecord(data.role) ? {
      id: asNumber(data.role.id),
      name: asString(data.role.name),
      description: asString(data.role.description),
    } : null
  }

  static from(input: unknown): S3GatewayCredentialVaultScope {
    return new S3GatewayCredentialVaultScope(input)
  }
}

const asVaultRef = (input: unknown): S3GatewayVaultRef | null => {
  if (!isRecord(input)) return null
  return {
    id: asNumber(input.id),
    name: asString(input.name),
    owner_id: asNullableNumber(input.owner_id),
  }
}

const asRoleRef = (input: unknown): S3GatewayVaultRoleRef | null => {
  if (!isRecord(input)) return null
  return {
    id: asNumber(input.id),
    name: asString(input.name),
    description: asString(input.description),
    permissions: Array.isArray(input.permissions)
      ? input.permissions.map(item => Permission.fromData({
        id: isRecord(item) ? asNumber(item.id) : undefined,
        bit_position: isRecord(item) ? asNumber(item.bit_position) : 0,
        qualified: isRecord(item) ? asString(item.qualified, asString(item.name)) : '',
        slug: isRecord(item) ? asString(item.slug) : '',
        description: isRecord(item) ? asString(item.description) : '',
        value: isRecord(item) ? asBoolean(item.value) : false,
      } as PermissionDTO))
      : [],
  }
}

const asPermission = (input: unknown): Permission | null => {
  if (!isRecord(input)) return null
  return Permission.fromData({
    id: asNumber(input.id),
    bit_position: asNumber(input.bit_position),
    qualified: asString(input.qualified, asString(input.name)),
    slug: asString(input.slug),
    description: asString(input.description),
    value: asBoolean(input.value),
  })
}

export class S3GatewayCredentialVaultRoleAssignment {
  id = 0
  assignment_id = 0
  credential_id = 0
  credential: S3GatewayCredential | null = null
  vault_id = 0
  vault: S3GatewayVaultRef | null = null
  vault_role_id = 0
  role: S3GatewayVaultRoleRef | null = null
  enabled = true
  created_by: number | null = null
  created_at: number | string | null = null
  updated_at: number | string | null = null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.id = asNumber(data.id, asNumber(data.assignment_id))
    this.assignment_id = asNumber(data.assignment_id, this.id)
    this.credential_id = asNumber(data.credential_id)
    this.credential = isRecord(data.credential) ? S3GatewayCredential.from(data.credential) : null
    this.vault_id = asNumber(data.vault_id)
    this.vault = asVaultRef(data.vault)
    this.vault_role_id = asNumber(data.vault_role_id)
    this.role = asRoleRef(data.role)
    this.enabled = asBoolean(data.enabled, true)
    this.created_by = asNullableNumber(data.created_by)
    this.created_at = asDateValue(data.created_at)
    this.updated_at = asDateValue(data.updated_at)
  }

  static from(input: unknown): S3GatewayCredentialVaultRoleAssignment {
    return new S3GatewayCredentialVaultRoleAssignment(input)
  }
}

export class S3GatewayCredentialVaultRoleOverride {
  id = 0
  override_id = 0
  assignment_id = 0
  credential_id = 0
  credential: S3GatewayCredential | null = null
  vault_id = 0
  vault: S3GatewayVaultRef | null = null
  permission_id = 0
  permission_name = ''
  permission_qualified = ''
  permission: Permission | null = null
  glob_path = ''
  effect: S3GatewayCredentialVaultRoleOverrideEffect = 'allow'
  enabled = true

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.id = asNumber(data.id, asNumber(data.override_id))
    this.override_id = asNumber(data.override_id, this.id)
    this.assignment_id = asNumber(data.assignment_id)
    this.credential_id = asNumber(data.credential_id)
    this.credential = isRecord(data.credential) ? S3GatewayCredential.from(data.credential) : null
    this.vault_id = asNumber(data.vault_id)
    this.vault = asVaultRef(data.vault)
    this.permission_id = asNumber(data.permission_id)
    this.permission_name = asString(data.permission_name)
    this.permission_qualified = asString(data.permission_qualified, this.permission_name)
    this.permission = asPermission(data.permission)
    this.glob_path = asString(data.glob_path)
    this.effect = asString(data.effect) === 'deny' ? 'deny' : 'allow'
    this.enabled = asBoolean(data.enabled, true)
  }

  static from(input: unknown): S3GatewayCredentialVaultRoleOverride {
    return new S3GatewayCredentialVaultRoleOverride(input)
  }
}

export class S3GatewayCredential {
  id = 0
  user_id = 0
  principal_user_id = 0
  principal_user: { id: number; name: string; email: string | null } | null = null
  created_by: number | null = null
  name = ''
  access_key = ''
  enabled = false
  enforce_budget_for_local_requests = false
  scope_mode: S3GatewayCredentialScopeMode = 'user_access'
  description: string | null = null
  created_at: number | string | null = null
  last_used_at: number | string | null = null
  expires_at: number | string | null = null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.id = asNumber(data.id)
    this.user_id = asNumber(data.user_id)
    this.principal_user_id = asNumber(data.principal_user_id)
    this.principal_user = isRecord(data.principal_user) ? {
      id: asNumber(data.principal_user.id),
      name: asString(data.principal_user.name),
      email: asNullableString(data.principal_user.email),
    } : null
    this.created_by = asNullableNumber(data.created_by)
    this.name = asString(data.name)
    this.access_key = asString(data.access_key)
    this.enabled = asBoolean(data.enabled)
    this.enforce_budget_for_local_requests = asBoolean(data.enforce_budget_for_local_requests)
    this.scope_mode = asGatewayScopeMode(data.scope_mode)
    this.description = asNullableString(data.description)
    this.created_at = asDateValue(data.created_at)
    this.last_used_at = asDateValue(data.last_used_at)
    this.expires_at = asDateValue(data.expires_at)
  }

  static from(input: unknown): S3GatewayCredential {
    return new S3GatewayCredential(input)
  }
}

export interface S3GatewayCredentialCreatePayload {
  name: string
  principal_user_id?: number | null
  user_id?: number | null
  scope_mode?: S3GatewayCredentialScopeMode
  description?: string | null
  expires_at?: number | null
  vault_scopes?: S3GatewayCredentialVaultScopePayload[]
  enforce_budget_for_local_requests?: boolean
}

export interface S3GatewayCredentialVaultScopePayload {
  vault_id: number
  can_list?: boolean
  can_read?: boolean
  can_write?: boolean
  can_delete?: boolean
  can_admin?: boolean
}

export interface S3GatewayCredentialScopeUpdatePayload {
  access_key?: string
  name?: string
  principal_user_id?: number | null
  scope_mode?: S3GatewayCredentialScopeMode
  description?: string | null
  expires_at?: number | null
  vault_scopes?: S3GatewayCredentialVaultScopePayload[]
  enforce_budget_for_local_requests?: boolean
}

export interface S3GatewayCredentialVaultRoleAssignmentPayload {
  credential_id?: number
  access_key?: string
  name?: string
  credential_name?: string
  vault_id?: number
  vault_name?: string
  vault?: string
  vault_role_id?: number
  vault_role_name?: string
  role_id?: number
  role_name?: string
  role?: string
  enabled?: boolean
}

export interface S3GatewayCredentialVaultRoleOverridePayload {
  credential_id?: number
  access_key?: string
  name?: string
  credential_name?: string
  vault_id?: number
  vault_name?: string
  vault?: string
  override_id?: number
  id?: number
  permission_id?: number
  permission_name?: string
  permission_qualified?: string
  permission?: string
  glob_path?: string
  path?: string
  effect?: S3GatewayCredentialVaultRoleOverrideEffect
  enabled?: boolean
}

export class S3GatewayBucketBinding {
  bucket_name = ''
  bucket = ''
  vault_id = 0
  mode: S3GatewayBucketMode = 'local'
  api_exclusive = false
  created_by: number | null = null
  created_at: number | string | null = null
  updated_at: number | string | null = null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.bucket_name = asString(data.bucket_name)
    this.bucket = asString(data.bucket, this.bucket_name)
    this.vault_id = asNumber(data.vault_id)
    this.mode = asString(data.mode, 'local')
    this.api_exclusive = asBoolean(data.api_exclusive)
    this.created_by = asNullableNumber(data.created_by)
    this.created_at = asDateValue(data.created_at)
    this.updated_at = asDateValue(data.updated_at)
  }

  static from(input: unknown): S3GatewayBucketBinding {
    return new S3GatewayBucketBinding(input)
  }
}

export interface S3GatewayBucketBindPayload {
  bucket_name: string
  vault_id: number
  mode?: S3GatewayBucketMode
  api_exclusive?: boolean
}

export interface S3GatewayCreateLocalBucketPayload {
  bucket_name: string
  owner_id?: number | null
  quota_bytes?: number | null
}

export interface S3GatewayCreateRemoteCachePayload {
  bucket_name: string
  api_key_id?: number | null
  api_key?: string | null
  upstream_bucket: string
  owner_id?: number | null
  description?: string | null
  encrypt_upstream?: boolean
}

export type S3GatewayBudgetPolicyPayload = PriceBudgetPolicyPayload
export type S3GatewayBudgetPolicy = PriceBudgetPolicy
export type S3GatewayBudgetStatus = PriceBudgetStatus
export type S3GatewayBudgetLedgerEntry = PriceBudgetLedgerEntry
