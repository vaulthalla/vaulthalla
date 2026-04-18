import { IFile } from '@/models/file'

/** -------------------------
 *  Shared literal unions
 *  ------------------------- */
export type ArtifactSide = 'local' | 'upstream'

export type ConflictType = 'mismatch' | 'encryption' | 'both'
export type ConflictResolution =
  | 'unresolved'
  | 'kept_local'
  | 'kept_remote'
  | 'kept_both'
  | 'overwritten'
  | 'fixed_remote_encryption'

export type ThroughputMetricType = 'upload' | 'download' | 'rename' | 'delete' | 'copy'

export type EventStatus = 'pending' | 'running' | 'success' | 'stalled' | 'error' | 'canceled'
export type EventTrigger = 'schedule' | 'manual' | 'startup' | 'webhook' | 'retry'

/** -------------------------
 *  Interfaces (as you had)
 *  ------------------------- */
export interface IArtifact {
  id: number
  conflict_id: number
  file: IFile
  side: ArtifactSide
}

export interface IConflictArtifact {
  local: IArtifact
  upstream: IArtifact
}

export interface IConflictReason {
  id: number
  conflict_id: number
  code: string
  message: string
}

export interface IConflict {
  id: number
  event_id: number
  file_id: number
  resolved_at: string | null
  created_at: string
  artifacts: IConflictArtifact
  reasons: IConflictReason[]
  type: ConflictType
  resolution: ConflictResolution
  failed_to_decrypted_upstream: boolean
}

export interface IThroughput {
  id: number
  run_uuid: string
  metric_type: ThroughputMetricType
  num_ops: number
  failed_ops: number
  size_bytes: number
  duration_ms: number
}

export interface IEvent {
  id: number
  vault_id: number
  run_uuid: string

  timestamp_begin: string
  timestamp_end: string | null
  heartbeat_at: string | null

  status: EventStatus
  trigger: EventTrigger
  retry_attempt: number

  stall_reason: string | null
  error_message: string | null
  error_code: string | null

  throughputs: IThroughput[]
  conflicts: IConflict[]

  num_ops_total: number
  num_failed_ops: number
  num_conflicts: number
  bytes_up: number
  bytes_down: number

  divergence_detected: boolean
  local_state_hash: string | null
  remote_state_hash: string | null
  config_hash: string | null
}

/** -------------------------
 *  Tiny runtime helpers
 *  ------------------------- */
function asObject(v: unknown): Record<string, unknown> {
  return v && typeof v === 'object' ? (v as Record<string, unknown>) : {}
}

function asArray<T>(v: unknown, map: (x: unknown) => T): T[] {
  return Array.isArray(v) ? v.map(map) : []
}

function asStringOrNull(v: unknown): string | null {
  return typeof v === 'string' ? v : null
}

function asBoolean(v: unknown, fallback = false): boolean {
  return typeof v === 'boolean' ? v : fallback
}

function asNumber(v: unknown, fallback = 0): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback
}

function asString(v: unknown, fallback = ''): string {
  return typeof v === 'string' ? v : fallback
}

/** -------------------------
 *  Classes (runtime-safe)
 *  ------------------------- */

export class Artifact implements IArtifact {
  id: number
  conflict_id: number
  file: IFile
  side: ArtifactSide

  constructor(raw: IArtifact) {
    this.id = raw.id
    this.conflict_id = raw.conflict_id
    this.file = raw.file
    this.side = raw.side
  }

  static from(raw: unknown): Artifact {
    const data = asObject(raw)
    // If file parsing needs to be stronger, wrap it here too.
    return new Artifact({
      id: asNumber(data.id),
      conflict_id: asNumber(data.conflict_id),
      file: data.file as IFile,
      side: data.side === 'local' || data.side === 'upstream' ? data.side : 'local',
    })
  }
}

export class ConflictReason implements IConflictReason {
  id: number
  conflict_id: number
  code: string
  message: string

  constructor(raw: IConflictReason) {
    this.id = raw.id
    this.conflict_id = raw.conflict_id
    this.code = raw.code
    this.message = raw.message
  }

  static from(raw: unknown): ConflictReason {
    const data = asObject(raw)
    return new ConflictReason({
      id: asNumber(data.id),
      conflict_id: asNumber(data.conflict_id),
      code: asString(data.code),
      message: asString(data.message),
    })
  }
}

export class ConflictArtifact implements IConflictArtifact {
  local: Artifact
  upstream: Artifact

  constructor(raw: IConflictArtifact) {
    this.local = new Artifact(raw.local)
    this.upstream = new Artifact(raw.upstream)
  }

  static from(raw: unknown): ConflictArtifact {
    const data = asObject(raw)
    return new ConflictArtifact({ local: Artifact.from(data.local), upstream: Artifact.from(data.upstream) })
  }
}

export class Conflict implements IConflict {
  id: number
  event_id: number
  file_id: number
  resolved_at: string | null
  created_at: string
  artifacts: ConflictArtifact
  reasons: ConflictReason[]
  type: ConflictType
  resolution: ConflictResolution
  failed_to_decrypted_upstream: boolean

  constructor(raw: IConflict) {
    this.id = raw.id
    this.event_id = raw.event_id
    this.file_id = raw.file_id
    this.resolved_at = raw.resolved_at
    this.created_at = raw.created_at
    this.artifacts = new ConflictArtifact(raw.artifacts)
    this.reasons = (raw.reasons ?? []).map(r => new ConflictReason(r))
    this.type = raw.type
    this.resolution = raw.resolution
    this.failed_to_decrypted_upstream = raw.failed_to_decrypted_upstream
  }

  static from(raw: unknown): Conflict {
    const data = asObject(raw)
    const type: ConflictType =
      data.type === 'mismatch' || data.type === 'encryption' || data.type === 'both' ? data.type : 'mismatch'

    const resolution: ConflictResolution =
      (
        data.resolution === 'unresolved'
        || data.resolution === 'kept_local'
        || data.resolution === 'kept_remote'
        || data.resolution === 'kept_both'
        || data.resolution === 'overwritten'
        || data.resolution === 'fixed_remote_encryption'
      ) ?
        data.resolution
      : 'unresolved'

    return new Conflict({
      id: asNumber(data.id),
      event_id: asNumber(data.event_id),
      file_id: asNumber(data.file_id),
      resolved_at: asStringOrNull(data.resolved_at),
      created_at: asString(data.created_at),
      artifacts: ConflictArtifact.from(data.artifacts),
      reasons: asArray(data.reasons, ConflictReason.from),
      type,
      resolution,
      failed_to_decrypted_upstream: asBoolean(data.failed_to_decrypted_upstream),
    })
  }
}

export class Throughput implements IThroughput {
  id: number
  run_uuid: string
  metric_type: ThroughputMetricType
  num_ops: number
  failed_ops: number
  size_bytes: number
  duration_ms: number

  constructor(raw: IThroughput) {
    this.id = raw.id
    this.run_uuid = raw.run_uuid
    this.metric_type = raw.metric_type
    this.num_ops = raw.num_ops
    this.failed_ops = raw.failed_ops
    this.size_bytes = raw.size_bytes
    this.duration_ms = raw.duration_ms
  }

  static from(raw: unknown): Throughput {
    const data = asObject(raw)
    const metric_type: ThroughputMetricType =
      (
        data.metric_type === 'upload'
        || data.metric_type === 'download'
        || data.metric_type === 'rename'
        || data.metric_type === 'delete'
        || data.metric_type === 'copy'
      ) ?
        data.metric_type
      : 'download'

    return new Throughput({
      id: asNumber(data.id),
      run_uuid: asString(data.run_uuid),
      metric_type,
      num_ops: asNumber(data.num_ops),
      failed_ops: asNumber(data.failed_ops),
      size_bytes: asNumber(data.size_bytes),
      duration_ms: asNumber(data.duration_ms),
    })
  }
}

export class SyncEvent implements IEvent {
  id: number
  vault_id: number
  run_uuid: string

  timestamp_begin: string
  timestamp_end: string | null
  heartbeat_at: string | null

  status: EventStatus
  trigger: EventTrigger
  retry_attempt: number

  stall_reason: string | null
  error_message: string | null
  error_code: string | null

  throughputs: Throughput[]
  conflicts: Conflict[]

  num_ops_total: number
  num_failed_ops: number
  num_conflicts: number
  bytes_up: number
  bytes_down: number

  divergence_detected: boolean
  local_state_hash: string | null
  remote_state_hash: string | null
  config_hash: string | null

  constructor(raw: IEvent) {
    this.id = raw.id
    this.vault_id = raw.vault_id
    this.run_uuid = raw.run_uuid

    this.timestamp_begin = raw.timestamp_begin
    this.timestamp_end = raw.timestamp_end
    this.heartbeat_at = raw.heartbeat_at

    this.status = raw.status
    this.trigger = raw.trigger
    this.retry_attempt = raw.retry_attempt

    this.stall_reason = raw.stall_reason
    this.error_message = raw.error_message
    this.error_code = raw.error_code

    this.throughputs = (raw.throughputs ?? []).map(t => new Throughput(t))
    this.conflicts = (raw.conflicts ?? []).map(c => new Conflict(c))

    this.num_ops_total = raw.num_ops_total
    this.num_failed_ops = raw.num_failed_ops
    this.num_conflicts = raw.num_conflicts
    this.bytes_up = raw.bytes_up
    this.bytes_down = raw.bytes_down

    this.divergence_detected = raw.divergence_detected
    this.local_state_hash = raw.local_state_hash
    this.remote_state_hash = raw.remote_state_hash
    this.config_hash = raw.config_hash
  }

  static from(raw: unknown): SyncEvent {
    const data = asObject(raw)
    const status: EventStatus =
      (
        data.status === 'pending'
        || data.status === 'running'
        || data.status === 'success'
        || data.status === 'stalled'
        || data.status === 'error'
        || data.status === 'canceled'
      ) ?
        data.status
      : 'pending'

    const trigger: EventTrigger =
      (
        data.trigger === 'schedule'
        || data.trigger === 'manual'
        || data.trigger === 'startup'
        || data.trigger === 'webhook'
        || data.trigger === 'retry'
      ) ?
        data.trigger
      : 'manual'

    return new SyncEvent({
      id: asNumber(data.id),
      vault_id: asNumber(data.vault_id),
      run_uuid: asString(data.run_uuid),

      timestamp_begin: asString(data.timestamp_begin),
      timestamp_end: asStringOrNull(data.timestamp_end),
      heartbeat_at: asStringOrNull(data.heartbeat_at),

      status,
      trigger,
      retry_attempt: asNumber(data.retry_attempt),

      stall_reason: asStringOrNull(data.stall_reason),
      error_message: asStringOrNull(data.error_message),
      error_code: asStringOrNull(data.error_code),

      throughputs: asArray(data.throughputs, Throughput.from),
      conflicts: asArray(data.conflicts, Conflict.from),

      num_ops_total: asNumber(data.num_ops_total),
      num_failed_ops: asNumber(data.num_failed_ops),
      num_conflicts: asNumber(data.num_conflicts),
      bytes_up: asNumber(data.bytes_up),
      bytes_down: asNumber(data.bytes_down),

      divergence_detected: asBoolean(data.divergence_detected),
      local_state_hash: asStringOrNull(data.local_state_hash),
      remote_state_hash: asStringOrNull(data.remote_state_hash),
      config_hash: asStringOrNull(data.config_hash),
    })
  }
}
