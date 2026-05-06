export type StorageBackendStatus = 'healthy' | 'degraded' | 'error' | 'unknown'
export type StorageVaultType = 'local' | 's3' | 'unknown'

type UnknownRecord = Record<string, unknown>

function isRecord(value: unknown): value is UnknownRecord {
  return !!value && typeof value === 'object' && !Array.isArray(value)
}

function asNumber(value: unknown, fallback = 0): number {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'string' && value.trim() !== '') {
    const parsed = Number(value)
    if (Number.isFinite(parsed)) return parsed
  }
  return fallback
}

function asNullableNumber(value: unknown): number | null {
  if (value == null) return null
  const parsed = asNumber(value, Number.NaN)
  return Number.isFinite(parsed) ? parsed : null
}

function asBoolean(value: unknown, fallback = false): boolean {
  return typeof value === 'boolean' ? value : fallback
}

function asNullableBoolean(value: unknown): boolean | null {
  return typeof value === 'boolean' ? value : null
}

function asNullableString(value: unknown): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null
}

function asNullableDate(value: unknown): number | string | null {
  return typeof value === 'string' ? value : asNullableNumber(value)
}

function asStatus(value: unknown): StorageBackendStatus {
  return value === 'healthy' || value === 'degraded' || value === 'error' ? value : 'unknown'
}

function asVaultType(value: unknown): StorageVaultType {
  return value === 'local' || value === 's3' ? value : 'unknown'
}

export class StorageVaultBackendStats {
  vault_id: number
  vault_name: string
  type: StorageVaultType
  active: boolean
  allow_fs_write: boolean
  quota_bytes: number
  free_space_bytes: number | null
  cache_size_bytes: number | null
  vault_size_bytes: number | null
  backend_status: StorageBackendStatus
  min_free_space_ok: boolean | null
  upstream_encryption_enabled: boolean | null
  bucket: string | null
  provider_ops_total: number | null
  provider_errors_total: number | null
  provider_error_rate: number | null
  provider_latency_avg_ms: number | null
  provider_latency_max_ms: number | null
  last_provider_error: string | null
  last_provider_success_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNumber(data.vault_id)
    this.vault_name = typeof data.vault_name === 'string' && data.vault_name.length > 0 ? data.vault_name : 'unknown vault'
    this.type = asVaultType(data.type)
    this.active = asBoolean(data.active)
    this.allow_fs_write = asBoolean(data.allow_fs_write)
    this.quota_bytes = asNumber(data.quota_bytes)
    this.free_space_bytes = asNullableNumber(data.free_space_bytes)
    this.cache_size_bytes = asNullableNumber(data.cache_size_bytes)
    this.vault_size_bytes = asNullableNumber(data.vault_size_bytes)
    this.backend_status = asStatus(data.backend_status)
    this.min_free_space_ok = asNullableBoolean(data.min_free_space_ok)
    this.upstream_encryption_enabled = asNullableBoolean(data.upstream_encryption_enabled)
    this.bucket = asNullableString(data.bucket)
    this.provider_ops_total = asNullableNumber(data.provider_ops_total)
    this.provider_errors_total = asNullableNumber(data.provider_errors_total)
    this.provider_error_rate = asNullableNumber(data.provider_error_rate)
    this.provider_latency_avg_ms = asNullableNumber(data.provider_latency_avg_ms)
    this.provider_latency_max_ms = asNullableNumber(data.provider_latency_max_ms)
    this.last_provider_error = asNullableString(data.last_provider_error)
    this.last_provider_success_at = asNullableDate(data.last_provider_success_at)
  }

  static from(input: unknown): StorageVaultBackendStats {
    return new StorageVaultBackendStats(input)
  }

  knownUsageBytes(): number {
    return Math.max(0, this.vault_size_bytes ?? 0) + Math.max(0, this.cache_size_bytes ?? 0)
  }

  hasProviderInstrumentation(): boolean {
    return this.provider_ops_total != null || this.provider_errors_total != null || this.provider_latency_avg_ms != null
  }
}

export class StorageBackendStats {
  overall_status: StorageBackendStatus
  vault_count_total: number
  local_vault_count: number
  s3_vault_count: number
  active_vault_count: number
  inactive_vault_count: number
  degraded_vault_count: number
  error_vault_count: number
  vaults: StorageVaultBackendStats[]
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.overall_status = asStatus(data.overall_status)
    this.vault_count_total = asNumber(data.vault_count_total)
    this.local_vault_count = asNumber(data.local_vault_count)
    this.s3_vault_count = asNumber(data.s3_vault_count)
    this.active_vault_count = asNumber(data.active_vault_count)
    this.inactive_vault_count = asNumber(data.inactive_vault_count)
    this.degraded_vault_count = asNumber(data.degraded_vault_count)
    this.error_vault_count = asNumber(data.error_vault_count)
    this.vaults = Array.isArray(data.vaults) ? data.vaults.map(StorageVaultBackendStats.from) : []
    this.checked_at = asNullableDate(data.checked_at)
  }

  static from(input: unknown): StorageBackendStats {
    return new StorageBackendStats(input)
  }

  totalFreeSpaceBytes(): number {
    return this.vaults.reduce((total, vault) => total + Math.max(0, vault.free_space_bytes ?? 0), 0)
  }

  totalKnownUsageBytes(): number {
    return this.vaults.reduce((total, vault) => total + vault.knownUsageBytes(), 0)
  }

  problemVaultCount(): number {
    return this.degraded_vault_count + this.error_vault_count
  }
}
