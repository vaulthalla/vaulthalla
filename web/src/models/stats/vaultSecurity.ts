export type VaultSecurityOverallStatus = 'healthy' | 'warning' | 'critical' | 'unknown'
export type VaultEncryptionStatus = 'encrypted' | 'mixed' | 'unknown'
export type VaultIntegrityCheckStatus = 'not_available' | 'not_run' | 'running' | 'passed' | 'failed' | 'unknown'

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

function asNullableString(value: unknown): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null
}

function asOverallStatus(value: unknown): VaultSecurityOverallStatus {
  return value === 'healthy' || value === 'warning' || value === 'critical' ? value : 'unknown'
}

function asEncryptionStatus(value: unknown): VaultEncryptionStatus {
  return value === 'encrypted' || value === 'mixed' ? value : 'unknown'
}

function asIntegrityStatus(value: unknown): VaultIntegrityCheckStatus {
  if (value === 'not_available' || value === 'not_run' || value === 'running' || value === 'passed' || value === 'failed') {
    return value
  }
  return 'unknown'
}

export class VaultSecurity {
  vault_id: number
  overall_status: VaultSecurityOverallStatus
  encryption_status: VaultEncryptionStatus
  current_key_version: number | null
  key_created_at: number | string | null
  key_age_days: number | null
  trashed_key_versions_count: number
  file_count: number
  files_current_key_version: number
  files_legacy_key_version: number
  files_unknown_key_version: number
  unauthorized_access_attempts_24h: number
  rate_limited_attempts_24h: number
  last_denied_access_at: number | string | null
  last_denied_access_reason: string | null
  last_permission_change_at: number | string | null
  last_share_policy_change_at: number | string | null
  integrity_check_status: VaultIntegrityCheckStatus
  last_integrity_check_at: number | string | null
  checksum_mismatch_count: number | null
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNumber(data.vault_id)
    this.overall_status = asOverallStatus(data.overall_status)
    this.encryption_status = asEncryptionStatus(data.encryption_status)
    this.current_key_version = asNullableNumber(data.current_key_version)
    this.key_created_at = typeof data.key_created_at === 'string' ? data.key_created_at : asNullableNumber(data.key_created_at)
    this.key_age_days = asNullableNumber(data.key_age_days)
    this.trashed_key_versions_count = asNumber(data.trashed_key_versions_count)
    this.file_count = asNumber(data.file_count)
    this.files_current_key_version = asNumber(data.files_current_key_version)
    this.files_legacy_key_version = asNumber(data.files_legacy_key_version)
    this.files_unknown_key_version = asNumber(data.files_unknown_key_version)
    this.unauthorized_access_attempts_24h = asNumber(data.unauthorized_access_attempts_24h)
    this.rate_limited_attempts_24h = asNumber(data.rate_limited_attempts_24h)
    this.last_denied_access_at =
      typeof data.last_denied_access_at === 'string' ? data.last_denied_access_at : asNullableNumber(data.last_denied_access_at)
    this.last_denied_access_reason = asNullableString(data.last_denied_access_reason)
    this.last_permission_change_at =
      typeof data.last_permission_change_at === 'string' ? data.last_permission_change_at : asNullableNumber(data.last_permission_change_at)
    this.last_share_policy_change_at =
      typeof data.last_share_policy_change_at === 'string' ? data.last_share_policy_change_at : asNullableNumber(data.last_share_policy_change_at)
    this.integrity_check_status = asIntegrityStatus(data.integrity_check_status)
    this.last_integrity_check_at =
      typeof data.last_integrity_check_at === 'string' ? data.last_integrity_check_at : asNullableNumber(data.last_integrity_check_at)
    this.checksum_mismatch_count = asNullableNumber(data.checksum_mismatch_count)
    this.checked_at = typeof data.checked_at === 'string' ? data.checked_at : asNullableNumber(data.checked_at)
  }

  static from(input: unknown): VaultSecurity {
    return new VaultSecurity(input)
  }

  legacyCoverage(): number {
    return this.file_count > 0 ? (this.files_legacy_key_version + this.files_unknown_key_version) / this.file_count : 0
  }

  hasProblems(): boolean {
    return this.overall_status === 'warning' || this.overall_status === 'critical' || this.encryption_status !== 'encrypted'
  }
}
