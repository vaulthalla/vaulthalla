// S3 gateway (s3.gateway.*). Normalizers keep unknown values unknown (null), never invent a running service.
import { bool, num, rec, str } from '@/features/cost/model'

export type ScopeMode = 'user_access' | 'vault_allowlist' | 'global'

export const SCOPE_MODES: { value: ScopeMode; label: string; hint: string }[] = [
  {
    value: 'user_access',
    label: 'Same as the user',
    hint: 'The key acts with its principal’s own vault roles and overrides. No extra gateway policy.',
  },
  {
    value: 'vault_allowlist',
    label: 'Selected vaults',
    hint: 'Only the vaults you select, with a default vault role (and optional per-vault exceptions).',
  },
  {
    value: 'global',
    label: 'All bound buckets',
    hint: 'Every bucket binding, with a default vault role. Needs an admin principal and manage_credentials.',
  },
]

export const scopeModeLabel = (mode: string | null) => SCOPE_MODES.find(m => m.value === mode)?.label ?? mode ?? '—'

const asScopeMode = (v: unknown): ScopeMode | null => {
  if (typeof v !== 'string') return null
  const n = v.toLowerCase().replaceAll('-', '_')
  return n === 'user_access' || n === 'vault_allowlist' || n === 'global' ? n : null
}

export interface GatewayStatus {
  running: boolean | null
  configured: boolean | null
  ready: boolean | null
  host: string | null
  port: number | null
  active_sessions: number | null
  total_requests: number | null
  failed_requests: number | null
}

export const toStatus = (input: unknown): GatewayStatus | null => {
  if (!input || typeof input !== 'object') return null
  const d = rec(input)
  return {
    running: bool(d.running),
    configured: bool(d.configured),
    ready: bool(d.ready),
    host: str(d.host),
    port: num(d.port),
    active_sessions: num(d.active_sessions),
    total_requests: num(d.total_requests),
    failed_requests: num(d.failed_requests),
  }
}

const WILDCARD = new Set(['0.0.0.0', '::', '[::]', '*', ''])

// Core reports the listener's bind address, not a URL clients can use. A wildcard bind is reachable on whatever
// host the operator exposes, so the console only proposes a URL (and says so) instead of inventing one.
export const bindAddress = (s: GatewayStatus | null) =>
  s && s.host !== null && s.port !== null ? `${s.host}:${s.port}` : null
export const isWildcardBind = (s: GatewayStatus | null) => Boolean(s && WILDCARD.has(s.host ?? ''))
export const suggestedEndpoint = (s: GatewayStatus | null, browserHost: string) => {
  if (!s || s.port === null) return ''
  const host = isWildcardBind(s) ? browserHost || 'localhost' : s.host
  return `http://${host}:${s.port}`
}

export interface PrincipalRef {
  id: number
  name: string
  email: string | null
}

export interface GatewayCredential {
  id: number
  name: string
  access_key: string
  principal_user_id: number | null
  principal_user: PrincipalRef | null
  enabled: boolean | null
  scope_mode: ScopeMode | null
  enforce_budget_for_local_requests: boolean
  description: string | null
  created_at: string | null
  last_used_at: string | null
  expires_at: string | null
}

export const toCredential = (input: unknown): GatewayCredential => {
  const d = rec(input)
  const p = d.principal_user ? rec(d.principal_user) : null
  return {
    id: num(d.id) ?? 0,
    name: str(d.name) ?? '',
    access_key: str(d.access_key) ?? '',
    principal_user_id: num(d.principal_user_id),
    principal_user: p ? { id: num(p.id) ?? 0, name: str(p.name) ?? '', email: str(p.email) } : null,
    enabled: bool(d.enabled),
    scope_mode: asScopeMode(d.scope_mode),
    enforce_budget_for_local_requests: bool(d.enforce_budget_for_local_requests) ?? false,
    description: str(d.description),
    created_at: str(d.created_at),
    last_used_at: str(d.last_used_at),
    expires_at: str(d.expires_at),
  }
}

export interface VaultRef {
  id: number
  name: string
}

const toVaultRef = (v: unknown): VaultRef | null => {
  if (!v || typeof v !== 'object') return null
  const d = rec(v)
  const id = num(d.id)
  return id === null ? null : { id, name: str(d.name) ?? `Vault ${id}` }
}

export interface RoleRef {
  id: number
  name: string
}

const toRoleRef = (v: unknown): RoleRef | null => {
  if (!v || typeof v !== 'object') return null
  const d = rec(v)
  const id = num(d.id)
  return id === null ? null : { id, name: str(d.name) ?? `Role ${id}` }
}

export interface DefaultRole {
  vault_role_id: number | null
  role: RoleRef | null
  enabled: boolean
}

export const toDefaultRole = (input: unknown): DefaultRole | null => {
  if (!input || typeof input !== 'object') return null
  const d = rec(input)
  return { vault_role_id: num(d.vault_role_id), role: toRoleRef(d.role), enabled: bool(d.enabled) ?? true }
}

export interface SelectedVault {
  vault_id: number
  vault: VaultRef | null
  enabled: boolean
}

export const toSelectedVault = (input: unknown): SelectedVault => {
  const d = rec(input)
  return { vault_id: num(d.vault_id) ?? 0, vault: toVaultRef(d.vault), enabled: bool(d.enabled) ?? true }
}

export interface RoleAssignment {
  id: number
  vault_id: number
  vault: VaultRef | null
  vault_role_id: number | null
  role: RoleRef | null
  enabled: boolean
}

export const toAssignment = (input: unknown): RoleAssignment => {
  const d = rec(input)
  return {
    id: num(d.id) ?? num(d.assignment_id) ?? 0,
    vault_id: num(d.vault_id) ?? 0,
    vault: toVaultRef(d.vault),
    vault_role_id: num(d.vault_role_id),
    role: toRoleRef(d.role),
    enabled: bool(d.enabled) ?? true,
  }
}

export interface PathOverride {
  id: number
  permission: string
  glob_path: string
  effect: 'allow' | 'deny'
  enabled: boolean
}

export const toOverride = (input: unknown): PathOverride => {
  const d = rec(input)
  const perm = rec(d.permission)
  return {
    id: num(d.id) ?? num(d.override_id) ?? 0,
    permission: str(d.permission_qualified) ?? str(d.permission_name) ?? str(perm.qualified) ?? str(perm.name) ?? '—',
    glob_path: str(d.glob_path) ?? '',
    effect: d.effect === 'deny' ? 'deny' : 'allow',
    enabled: bool(d.enabled) ?? true,
  }
}

export interface BucketBinding {
  bucket_name: string
  vault_id: number | null
  mode: string | null
  api_exclusive: boolean | null
  created_at: string | null
}

export const toBucket = (input: unknown): BucketBinding => {
  const d = rec(input)
  return {
    bucket_name: str(d.bucket_name) ?? str(d.bucket) ?? '',
    vault_id: num(d.vault_id),
    mode: str(d.mode),
    api_exclusive: bool(d.api_exclusive),
    created_at: str(d.created_at),
  }
}

export const BUCKET_MODES: Record<string, { label: string; hint: string }> = {
  local: { label: 'Local', hint: 'Objects live in the vault on this server.' },
  remote_cache: { label: 'Remote cache', hint: 'Served from local cache, synced to the vault’s S3 bucket.' },
  remote_proxy: { label: 'Remote proxy', hint: 'Requests pass through to the vault’s S3 bucket.' },
}

// S3 bucket naming (the gateway enforces the full rules; this catches the common mistakes early).
export const bucketNameProblem = (name: string) => {
  if (!name) return null
  if (name.length < 3 || name.length > 63) return 'Use 3–63 characters'
  if (!/^[a-z0-9][a-z0-9.-]*[a-z0-9]$/.test(name))
    return 'Lowercase letters, digits, dots and hyphens; start and end with a letter or digit'
  return null
}
