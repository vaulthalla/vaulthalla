import { IEvent, SyncEvent } from '@/models/stats/sync'

export interface ICapacityStats {
  capacity: number
  logical_size: number
  physical_size: number
  cache_size: number
  free_space: number
  file_count: number
  directory_count: number
  average_file_size: number
  largest_file_size: number
  top_file_extensions: Record<string, number>
}

export interface IVaultStats {
  capacity: ICapacityStats
  latest_sync_event: IEvent | null
}

function asObject(v: unknown): Record<string, unknown> {
  return v && typeof v === 'object' ? (v as Record<string, unknown>) : {}
}

/** -------------------------
 *  Tiny runtime helpers
 *  ------------------------- */
function asNumber(v: unknown, fallback = 0): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback
}

function asRecordNumber(v: unknown): Record<string, number> {
  if (!v || typeof v !== 'object' || Array.isArray(v)) return {}
  const out: Record<string, number> = {}
  for (const [k, val] of Object.entries(v as Record<string, unknown>)) {
    out[k] = asNumber(val, 0)
  }
  return out
}

/** -------------------------
 *  Classes
 *  ------------------------- */
export class CapacityStats implements ICapacityStats {
  capacity = 0
  logical_size = 0
  physical_size = 0
  cache_size = 0
  free_space = 0
  file_count = 0
  directory_count = 0
  average_file_size = 0
  largest_file_size = 0
  top_file_extensions: Record<string, number> = {}

  constructor(data?: Partial<ICapacityStats>) {
    if (!data) return
    this.capacity = data.capacity ?? this.capacity
    this.logical_size = data.logical_size ?? this.logical_size
    this.physical_size = data.physical_size ?? this.physical_size
    this.cache_size = data.cache_size ?? this.cache_size
    this.free_space = data.free_space ?? this.free_space
    this.file_count = data.file_count ?? this.file_count
    this.directory_count = data.directory_count ?? this.directory_count
    this.average_file_size = data.average_file_size ?? this.average_file_size
    this.largest_file_size = data.largest_file_size ?? this.largest_file_size
    this.top_file_extensions = data.top_file_extensions ?? this.top_file_extensions
  }

  static from(raw: unknown): CapacityStats {
    const data = asObject(raw)
    return new CapacityStats({
      capacity: asNumber(data.capacity),
      logical_size: asNumber(data.logical_size),
      physical_size: asNumber(data.physical_size),
      cache_size: asNumber(data.cache_size),
      free_space: asNumber(data.free_space),
      file_count: asNumber(data.file_count),
      directory_count: asNumber(data.directory_count),
      average_file_size: asNumber(data.average_file_size),
      largest_file_size: asNumber(data.largest_file_size),
      top_file_extensions: asRecordNumber(data.top_file_extensions),
    })
  }
}

export class VaultStats implements IVaultStats {
  capacity: CapacityStats
  latest_sync_event: IEvent | null

  constructor(data?: Partial<IVaultStats>) {
    this.capacity = new CapacityStats(data?.capacity)
    this.latest_sync_event = data?.latest_sync_event ?? null
  }

  static from(raw: unknown): VaultStats {
    const data = asObject(raw)
    const capacity = CapacityStats.from(data.capacity)

    // Prefer a real SyncEvent instance (runtime safe), but allow null if missing.
    const latest =
      data.latest_sync_event && typeof data.latest_sync_event === 'object' ? SyncEvent.from(data.latest_sync_event) : null

    return new VaultStats({ capacity, latest_sync_event: latest })
  }
}
