export type CleanupStatus = 'healthy' | 'warning' | 'overdue' | 'unknown'

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

function asStatus(value: unknown): CleanupStatus {
  return value === 'healthy' || value === 'warning' || value === 'overdue' ? value : 'unknown'
}

export class RetentionStats {
  vault_id: number | null
  scope: 'system' | 'vault'
  cleanup_status: CleanupStatus
  trash_retention_days: number
  sync_event_retention_days: number
  sync_event_max_entries: number
  audit_log_retention_days: number
  cache_expiry_days: number
  cache_max_size_bytes: number
  trashed_files_count: number
  trashed_bytes_total: number
  oldest_trashed_age_seconds: number | null
  trashed_files_past_retention_count: number
  trashed_bytes_past_retention: number
  sync_events_total: number
  sync_events_past_retention_count: number
  audit_log_entries_total: number
  oldest_audit_log_age_seconds: number | null
  audit_log_entries_past_retention_count: number
  share_access_events_total: number
  oldest_share_access_event_age_seconds: number | null
  cache_entries_total: number
  cache_bytes_total: number
  cache_eviction_candidates: number
  cache_entries_expired: number
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNullableNumber(data.vault_id)
    this.scope = data.scope === 'vault' ? 'vault' : 'system'
    this.cleanup_status = asStatus(data.cleanup_status)
    this.trash_retention_days = asNumber(data.trash_retention_days)
    this.sync_event_retention_days = asNumber(data.sync_event_retention_days)
    this.sync_event_max_entries = asNumber(data.sync_event_max_entries)
    this.audit_log_retention_days = asNumber(data.audit_log_retention_days)
    this.cache_expiry_days = asNumber(data.cache_expiry_days)
    this.cache_max_size_bytes = asNumber(data.cache_max_size_bytes)
    this.trashed_files_count = asNumber(data.trashed_files_count)
    this.trashed_bytes_total = asNumber(data.trashed_bytes_total)
    this.oldest_trashed_age_seconds = asNullableNumber(data.oldest_trashed_age_seconds)
    this.trashed_files_past_retention_count = asNumber(data.trashed_files_past_retention_count)
    this.trashed_bytes_past_retention = asNumber(data.trashed_bytes_past_retention)
    this.sync_events_total = asNumber(data.sync_events_total)
    this.sync_events_past_retention_count = asNumber(data.sync_events_past_retention_count)
    this.audit_log_entries_total = asNumber(data.audit_log_entries_total)
    this.oldest_audit_log_age_seconds = asNullableNumber(data.oldest_audit_log_age_seconds)
    this.audit_log_entries_past_retention_count = asNumber(data.audit_log_entries_past_retention_count)
    this.share_access_events_total = asNumber(data.share_access_events_total)
    this.oldest_share_access_event_age_seconds = asNullableNumber(data.oldest_share_access_event_age_seconds)
    this.cache_entries_total = asNumber(data.cache_entries_total)
    this.cache_bytes_total = asNumber(data.cache_bytes_total)
    this.cache_eviction_candidates = asNumber(data.cache_eviction_candidates)
    this.cache_entries_expired = asNumber(data.cache_entries_expired)
    this.checked_at = asNullableDate(data.checked_at)
  }

  static from(input: unknown): RetentionStats {
    return new RetentionStats(input)
  }

  pastRetentionCount(): number {
    return (
      this.trashed_files_past_retention_count +
      this.sync_events_past_retention_count +
      this.audit_log_entries_past_retention_count +
      this.cache_entries_expired
    )
  }

  hasCleanupPressure(): boolean {
    return this.cleanup_status === 'warning' || this.cleanup_status === 'overdue'
  }
}
