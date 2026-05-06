import { SyncEvent } from '@/models/stats/sync'

export type VaultSyncOverallStatus = 'healthy' | 'syncing' | 'warning' | 'stalled' | 'error' | 'diverged' | 'unknown'
export type VaultSyncCurrentState =
  | 'idle'
  | 'pending'
  | 'running'
  | 'stalled'
  | 'error'
  | 'cancelled'
  | 'success'
  | 'diverged'
  | 'unknown'

export interface IVaultSyncHealth {
  vault_id: number
  overall_status: VaultSyncOverallStatus
  current_state: VaultSyncCurrentState
  sync_enabled: boolean
  sync_interval_seconds: number
  configured_strategy: string | null
  conflict_policy: string | null
  latest_event: SyncEvent | null
  latest_event_id: number | null
  latest_run_uuid: string | null
  last_sync_at: number | string | null
  last_success_at: number | string | null
  last_success_age_seconds: number | null
  active_run_count: number
  pending_run_count: number
  running_run_count: number
  stalled_run_count: number
  oldest_active_age_seconds: number | null
  latest_heartbeat_age_seconds: number | null
  error_count_24h: number
  error_count_7d: number
  failed_ops_24h: number
  failed_ops_7d: number
  retry_count_24h: number
  retry_count_7d: number
  conflict_count_open: number
  conflict_count_24h: number
  conflict_count_7d: number
  bytes_up_24h: number
  bytes_down_24h: number
  bytes_total_24h: number
  bytes_up_7d: number
  bytes_down_7d: number
  bytes_total_7d: number
  avg_throughput_bytes_per_sec_24h: number
  peak_throughput_bytes_per_sec_24h: number
  divergence_detected: boolean
  local_state_hash: string | null
  remote_state_hash: string | null
  hash_mismatch: boolean
  last_error_code: string | null
  last_error_message: string | null
  last_stall_reason: string | null
  checked_at: number | string | null
}

function asObject(v: unknown): Record<string, unknown> {
  return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : {}
}

function asNumber(v: unknown, fallback = 0): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback
}

function asNullableNumber(v: unknown): number | null {
  return typeof v === 'number' && Number.isFinite(v) ? v : null
}

function asBoolean(v: unknown, fallback = false): boolean {
  return typeof v === 'boolean' ? v : fallback
}

function asStringOrNull(v: unknown): string | null {
  return typeof v === 'string' && v.length > 0 ? v : null
}

function asCheckedAt(v: unknown): number | string | null {
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.length > 0) return v
  return null
}

function asOverallStatus(v: unknown): VaultSyncOverallStatus {
  return (
      v === 'healthy'
        || v === 'syncing'
        || v === 'warning'
        || v === 'stalled'
        || v === 'error'
        || v === 'diverged'
        || v === 'unknown'
    ) ?
      v
    : 'unknown'
}

function asCurrentState(v: unknown): VaultSyncCurrentState {
  return (
      v === 'idle'
        || v === 'pending'
        || v === 'running'
        || v === 'stalled'
        || v === 'error'
        || v === 'cancelled'
        || v === 'success'
        || v === 'diverged'
        || v === 'unknown'
    ) ?
      v
    : 'unknown'
}

export class VaultSyncHealth implements IVaultSyncHealth {
  vault_id = 0
  overall_status: VaultSyncOverallStatus = 'unknown'
  current_state: VaultSyncCurrentState = 'unknown'
  sync_enabled = false
  sync_interval_seconds = 0
  configured_strategy: string | null = null
  conflict_policy: string | null = null
  latest_event: SyncEvent | null = null
  latest_event_id: number | null = null
  latest_run_uuid: string | null = null
  last_sync_at: number | string | null = null
  last_success_at: number | string | null = null
  last_success_age_seconds: number | null = null
  active_run_count = 0
  pending_run_count = 0
  running_run_count = 0
  stalled_run_count = 0
  oldest_active_age_seconds: number | null = null
  latest_heartbeat_age_seconds: number | null = null
  error_count_24h = 0
  error_count_7d = 0
  failed_ops_24h = 0
  failed_ops_7d = 0
  retry_count_24h = 0
  retry_count_7d = 0
  conflict_count_open = 0
  conflict_count_24h = 0
  conflict_count_7d = 0
  bytes_up_24h = 0
  bytes_down_24h = 0
  bytes_total_24h = 0
  bytes_up_7d = 0
  bytes_down_7d = 0
  bytes_total_7d = 0
  avg_throughput_bytes_per_sec_24h = 0
  peak_throughput_bytes_per_sec_24h = 0
  divergence_detected = false
  local_state_hash: string | null = null
  remote_state_hash: string | null = null
  hash_mismatch = false
  last_error_code: string | null = null
  last_error_message: string | null = null
  last_stall_reason: string | null = null
  checked_at: number | string | null = null

  constructor(data?: Partial<IVaultSyncHealth>) {
    if (!data) return
    Object.assign(this, data)
  }

  static from(raw: unknown): VaultSyncHealth {
    const data = asObject(raw)
    const latestEvent =
      data.latest_event && typeof data.latest_event === 'object' ? SyncEvent.from(data.latest_event) : null

    return new VaultSyncHealth({
      vault_id: asNumber(data.vault_id),
      overall_status: asOverallStatus(data.overall_status),
      current_state: asCurrentState(data.current_state),
      sync_enabled: asBoolean(data.sync_enabled),
      sync_interval_seconds: asNumber(data.sync_interval_seconds),
      configured_strategy: asStringOrNull(data.configured_strategy),
      conflict_policy: asStringOrNull(data.conflict_policy),
      latest_event: latestEvent,
      latest_event_id: asNullableNumber(data.latest_event_id),
      latest_run_uuid: asStringOrNull(data.latest_run_uuid),
      last_sync_at: asCheckedAt(data.last_sync_at),
      last_success_at: asCheckedAt(data.last_success_at),
      last_success_age_seconds: asNullableNumber(data.last_success_age_seconds),
      active_run_count: asNumber(data.active_run_count),
      pending_run_count: asNumber(data.pending_run_count),
      running_run_count: asNumber(data.running_run_count),
      stalled_run_count: asNumber(data.stalled_run_count),
      oldest_active_age_seconds: asNullableNumber(data.oldest_active_age_seconds),
      latest_heartbeat_age_seconds: asNullableNumber(data.latest_heartbeat_age_seconds),
      error_count_24h: asNumber(data.error_count_24h),
      error_count_7d: asNumber(data.error_count_7d),
      failed_ops_24h: asNumber(data.failed_ops_24h),
      failed_ops_7d: asNumber(data.failed_ops_7d),
      retry_count_24h: asNumber(data.retry_count_24h),
      retry_count_7d: asNumber(data.retry_count_7d),
      conflict_count_open: asNumber(data.conflict_count_open),
      conflict_count_24h: asNumber(data.conflict_count_24h),
      conflict_count_7d: asNumber(data.conflict_count_7d),
      bytes_up_24h: asNumber(data.bytes_up_24h),
      bytes_down_24h: asNumber(data.bytes_down_24h),
      bytes_total_24h: asNumber(data.bytes_total_24h),
      bytes_up_7d: asNumber(data.bytes_up_7d),
      bytes_down_7d: asNumber(data.bytes_down_7d),
      bytes_total_7d: asNumber(data.bytes_total_7d),
      avg_throughput_bytes_per_sec_24h: asNumber(data.avg_throughput_bytes_per_sec_24h),
      peak_throughput_bytes_per_sec_24h: asNumber(data.peak_throughput_bytes_per_sec_24h),
      divergence_detected: asBoolean(data.divergence_detected),
      local_state_hash: asStringOrNull(data.local_state_hash),
      remote_state_hash: asStringOrNull(data.remote_state_hash),
      hash_mismatch: asBoolean(data.hash_mismatch),
      last_error_code: asStringOrNull(data.last_error_code),
      last_error_message: asStringOrNull(data.last_error_message),
      last_stall_reason: asStringOrNull(data.last_stall_reason),
      checked_at: asCheckedAt(data.checked_at),
    })
  }

  hasProblems(): boolean {
    return (
      this.overall_status === 'warning'
      || this.overall_status === 'stalled'
      || this.overall_status === 'error'
      || this.overall_status === 'diverged'
    )
  }

  isActive(): boolean {
    return this.current_state === 'pending' || this.current_state === 'running' || this.current_state === 'stalled'
  }

  traffic24h(): number {
    return this.bytes_total_24h || this.bytes_up_24h + this.bytes_down_24h
  }

  traffic7d(): number {
    return this.bytes_total_7d || this.bytes_up_7d + this.bytes_down_7d
  }

  statusTone(): 'healthy' | 'syncing' | 'warning' | 'critical' | 'unknown' {
    if (this.overall_status === 'healthy') return 'healthy'
    if (this.overall_status === 'syncing') return 'syncing'
    if (this.overall_status === 'warning') return 'warning'
    if (this.overall_status === 'stalled' || this.overall_status === 'error' || this.overall_status === 'diverged')
      return 'critical'
    return 'unknown'
  }
}
