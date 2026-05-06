export type OperationQueueStatus = 'healthy' | 'warning' | 'critical' | 'unknown'

type UnknownRecord = Record<string, unknown>
type CountMap = Record<string, number>

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

function asStatus(value: unknown): OperationQueueStatus {
  return value === 'healthy' || value === 'warning' || value === 'critical' ? value : 'unknown'
}

function asCountMap(value: unknown, keys: string[]): CountMap {
  const data = isRecord(value) ? value : {}
  return keys.reduce<CountMap>((out, key) => {
    out[key] = asNumber(data[key])
    return out
  }, {})
}

export class RecentOperationError {
  source: string
  operation: string
  status: string
  target: string
  path: string
  error: string | null
  occurred_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.source = typeof data.source === 'string' ? data.source : 'unknown'
    this.operation = typeof data.operation === 'string' ? data.operation : 'unknown'
    this.status = typeof data.status === 'string' ? data.status : 'unknown'
    this.target = typeof data.target === 'string' ? data.target : 'unknown'
    this.path = typeof data.path === 'string' ? data.path : ''
    this.error = asNullableString(data.error)
    this.occurred_at = asNullableDate(data.occurred_at)
  }

  static from(input: unknown): RecentOperationError {
    return new RecentOperationError(input)
  }
}

export class OperationStats {
  vault_id: number | null
  scope: 'system' | 'vault'
  overall_status: OperationQueueStatus
  stale_threshold_seconds: number
  pending_operations: number
  in_progress_operations: number
  failed_operations_24h: number
  cancelled_operations_24h: number
  stalled_operations: number
  oldest_pending_operation_age_seconds: number | null
  oldest_in_progress_operation_age_seconds: number | null
  operations_by_type: CountMap
  operations_by_status: CountMap
  active_share_uploads: number
  stalled_share_uploads: number
  failed_share_uploads_24h: number
  upload_bytes_expected_active: number
  upload_bytes_received_active: number
  oldest_active_upload_age_seconds: number | null
  recent_operation_errors: RecentOperationError[]
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNullableNumber(data.vault_id)
    this.scope = data.scope === 'vault' ? 'vault' : 'system'
    this.overall_status = asStatus(data.overall_status)
    this.stale_threshold_seconds = asNumber(data.stale_threshold_seconds, 900)
    this.pending_operations = asNumber(data.pending_operations)
    this.in_progress_operations = asNumber(data.in_progress_operations)
    this.failed_operations_24h = asNumber(data.failed_operations_24h)
    this.cancelled_operations_24h = asNumber(data.cancelled_operations_24h)
    this.stalled_operations = asNumber(data.stalled_operations)
    this.oldest_pending_operation_age_seconds = asNullableNumber(data.oldest_pending_operation_age_seconds)
    this.oldest_in_progress_operation_age_seconds = asNullableNumber(data.oldest_in_progress_operation_age_seconds)
    this.operations_by_type = asCountMap(data.operations_by_type, ['move', 'copy', 'rename'])
    this.operations_by_status = asCountMap(data.operations_by_status, ['pending', 'in_progress', 'success', 'error', 'cancelled'])
    this.active_share_uploads = asNumber(data.active_share_uploads)
    this.stalled_share_uploads = asNumber(data.stalled_share_uploads)
    this.failed_share_uploads_24h = asNumber(data.failed_share_uploads_24h)
    this.upload_bytes_expected_active = asNumber(data.upload_bytes_expected_active)
    this.upload_bytes_received_active = asNumber(data.upload_bytes_received_active)
    this.oldest_active_upload_age_seconds = asNullableNumber(data.oldest_active_upload_age_seconds)
    this.recent_operation_errors = Array.isArray(data.recent_operation_errors)
      ? data.recent_operation_errors.map(RecentOperationError.from)
      : []
    this.checked_at = asNullableDate(data.checked_at)
  }

  static from(input: unknown): OperationStats {
    return new OperationStats(input)
  }

  activeWorkCount(): number {
    return this.pending_operations + this.in_progress_operations + this.active_share_uploads
  }

  stuckWorkCount(): number {
    return this.stalled_operations + this.stalled_share_uploads
  }

  failedWork24h(): number {
    return this.failed_operations_24h + this.failed_share_uploads_24h
  }
}
