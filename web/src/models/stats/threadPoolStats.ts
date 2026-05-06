export type ThreadPoolStatus = 'idle' | 'normal' | 'pressured' | 'saturated' | 'degraded' | 'unknown'
export type ThreadPoolManagerStatus = 'healthy' | 'pressured' | 'saturated' | 'degraded' | 'unknown'

export interface IThreadPoolStats {
  name: string
  queue_depth: number
  worker_count: number
  borrowed_worker_count: number
  idle_worker_count: number
  busy_worker_count: number
  has_idle_worker: boolean
  has_borrowed_worker: boolean
  stopped: boolean
  pressure_ratio: number
  status: ThreadPoolStatus
}

export interface IThreadPoolManagerStats {
  pools: ThreadPoolStats[]
  total_worker_count: number
  total_queue_depth: number
  total_borrowed_worker_count: number
  total_idle_worker_count: number
  max_pressure_ratio: number
  saturated_pool_count: number
  pressured_pool_count: number
  overall_status: ThreadPoolManagerStatus
  checked_at: number | string | null
}

function asObject(v: unknown): Record<string, unknown> {
  return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : {}
}

function asString(v: unknown, fallback = ''): string {
  return typeof v === 'string' ? v : fallback
}

function asNumber(v: unknown, fallback = 0): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : fallback
}

function asBoolean(v: unknown, fallback = false): boolean {
  return typeof v === 'boolean' ? v : fallback
}

function asThreadPoolStatus(v: unknown): ThreadPoolStatus {
  return v === 'idle' || v === 'normal' || v === 'pressured' || v === 'saturated' || v === 'degraded' ? v : 'unknown'
}

function asManagerStatus(v: unknown): ThreadPoolManagerStatus {
  return v === 'healthy' || v === 'pressured' || v === 'saturated' || v === 'degraded' ? v : 'unknown'
}

function asCheckedAt(v: unknown): number | string | null {
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.length > 0) return v
  return null
}

export class ThreadPoolStats implements IThreadPoolStats {
  name = 'unknown'
  queue_depth = 0
  worker_count = 0
  borrowed_worker_count = 0
  idle_worker_count = 0
  busy_worker_count = 0
  has_idle_worker = false
  has_borrowed_worker = false
  stopped = false
  pressure_ratio = 0
  status: ThreadPoolStatus = 'unknown'

  constructor(data?: Partial<IThreadPoolStats>) {
    if (!data) return
    this.name = data.name ?? this.name
    this.queue_depth = data.queue_depth ?? this.queue_depth
    this.worker_count = data.worker_count ?? this.worker_count
    this.borrowed_worker_count = data.borrowed_worker_count ?? this.borrowed_worker_count
    this.idle_worker_count = data.idle_worker_count ?? this.idle_worker_count
    this.busy_worker_count = data.busy_worker_count ?? this.busy_worker_count
    this.has_idle_worker = data.has_idle_worker ?? this.has_idle_worker
    this.has_borrowed_worker = data.has_borrowed_worker ?? this.has_borrowed_worker
    this.stopped = data.stopped ?? this.stopped
    this.pressure_ratio = data.pressure_ratio ?? this.pressure_ratio
    this.status = data.status ?? this.status
  }

  static from(raw: unknown): ThreadPoolStats {
    const data = asObject(raw)
    return new ThreadPoolStats({
      name: asString(data.name, 'unknown'),
      queue_depth: asNumber(data.queue_depth),
      worker_count: asNumber(data.worker_count),
      borrowed_worker_count: asNumber(data.borrowed_worker_count),
      idle_worker_count: asNumber(data.idle_worker_count),
      busy_worker_count: asNumber(data.busy_worker_count),
      has_idle_worker: asBoolean(data.has_idle_worker),
      has_borrowed_worker: asBoolean(data.has_borrowed_worker),
      stopped: asBoolean(data.stopped),
      pressure_ratio: asNumber(data.pressure_ratio),
      status: asThreadPoolStatus(data.status),
    })
  }
}

export class ThreadPoolManagerStats implements IThreadPoolManagerStats {
  pools: ThreadPoolStats[] = []
  total_worker_count = 0
  total_queue_depth = 0
  total_borrowed_worker_count = 0
  total_idle_worker_count = 0
  max_pressure_ratio = 0
  saturated_pool_count = 0
  pressured_pool_count = 0
  overall_status: ThreadPoolManagerStatus = 'unknown'
  checked_at: number | string | null = null

  constructor(data?: Partial<IThreadPoolManagerStats>) {
    if (!data) return
    this.pools = data.pools ?? this.pools
    this.total_worker_count = data.total_worker_count ?? this.total_worker_count
    this.total_queue_depth = data.total_queue_depth ?? this.total_queue_depth
    this.total_borrowed_worker_count = data.total_borrowed_worker_count ?? this.total_borrowed_worker_count
    this.total_idle_worker_count = data.total_idle_worker_count ?? this.total_idle_worker_count
    this.max_pressure_ratio = data.max_pressure_ratio ?? this.max_pressure_ratio
    this.saturated_pool_count = data.saturated_pool_count ?? this.saturated_pool_count
    this.pressured_pool_count = data.pressured_pool_count ?? this.pressured_pool_count
    this.overall_status = data.overall_status ?? this.overall_status
    this.checked_at = data.checked_at ?? this.checked_at
  }

  static from(raw: unknown): ThreadPoolManagerStats {
    const data = asObject(raw)
    const pools = Array.isArray(data.pools) ? data.pools.map(ThreadPoolStats.from) : []

    return new ThreadPoolManagerStats({
      pools,
      total_worker_count: asNumber(data.total_worker_count),
      total_queue_depth: asNumber(data.total_queue_depth),
      total_borrowed_worker_count: asNumber(data.total_borrowed_worker_count),
      total_idle_worker_count: asNumber(data.total_idle_worker_count),
      max_pressure_ratio: asNumber(data.max_pressure_ratio),
      saturated_pool_count: asNumber(data.saturated_pool_count),
      pressured_pool_count: asNumber(data.pressured_pool_count),
      overall_status: asManagerStatus(data.overall_status),
      checked_at: asCheckedAt(data.checked_at),
    })
  }
}
