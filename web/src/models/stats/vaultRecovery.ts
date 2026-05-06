export type VaultBackupStatus = 'idle' | 'syncing' | 'error' | 'disabled' | 'unknown'
export type VaultRecoveryReadiness = 'ready' | 'stale' | 'failing' | 'disabled' | 'unknown'

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

function asNullableDate(value: unknown): number | string | null {
  return typeof value === 'string' ? value : asNullableNumber(value)
}

function asNullableString(value: unknown): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null
}

function asBool(value: unknown, fallback = false): boolean {
  return typeof value === 'boolean' ? value : fallback
}

function asNullableBool(value: unknown): boolean | null {
  return typeof value === 'boolean' ? value : null
}

function asBackupStatus(value: unknown): VaultBackupStatus {
  return value === 'idle' || value === 'syncing' || value === 'error' || value === 'disabled' ? value : 'unknown'
}

function asReadiness(value: unknown): VaultRecoveryReadiness {
  return value === 'ready' || value === 'stale' || value === 'failing' || value === 'disabled' ? value : 'unknown'
}

export class VaultRecovery {
  vault_id: number
  backup_policy_present: boolean
  backup_enabled: boolean
  backup_status: VaultBackupStatus
  backup_interval_seconds: number
  retention_seconds: number | null
  last_backup_at: number | string | null
  last_success_at: number | string | null
  last_success_age_seconds: number | null
  next_expected_backup_at: number | string | null
  backup_stale: boolean | null
  missed_backup_count_estimate: number | null
  retry_count: number
  last_error: string | null
  recovery_readiness: VaultRecoveryReadiness
  time_since_last_verified_good_state: number | null
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNumber(data.vault_id)
    this.backup_policy_present = asBool(data.backup_policy_present)
    this.backup_enabled = asBool(data.backup_enabled)
    this.backup_status = asBackupStatus(data.backup_status)
    this.backup_interval_seconds = asNumber(data.backup_interval_seconds)
    this.retention_seconds = asNullableNumber(data.retention_seconds)
    this.last_backup_at = asNullableDate(data.last_backup_at)
    this.last_success_at = asNullableDate(data.last_success_at)
    this.last_success_age_seconds = asNullableNumber(data.last_success_age_seconds)
    this.next_expected_backup_at = asNullableDate(data.next_expected_backup_at)
    this.backup_stale = asNullableBool(data.backup_stale)
    this.missed_backup_count_estimate = asNullableNumber(data.missed_backup_count_estimate)
    this.retry_count = asNumber(data.retry_count)
    this.last_error = asNullableString(data.last_error)
    this.recovery_readiness = asReadiness(data.recovery_readiness)
    this.time_since_last_verified_good_state = asNullableNumber(data.time_since_last_verified_good_state)
    this.checked_at = asNullableDate(data.checked_at)
  }

  static from(input: unknown): VaultRecovery {
    return new VaultRecovery(input)
  }

  hasProblems(): boolean {
    return this.recovery_readiness === 'stale' || this.recovery_readiness === 'failing' || this.recovery_readiness === 'unknown'
  }

  isReady(): boolean {
    return this.recovery_readiness === 'ready'
  }
}
