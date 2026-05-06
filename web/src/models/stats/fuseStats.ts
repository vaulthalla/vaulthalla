export interface IFuseOpStats {
  op: string
  count: number
  successes: number
  errors: number
  bytes_read: number
  bytes_written: number
  total_us: number
  max_us: number
  avg_ms: number
  max_ms: number
  error_rate: number
}

export interface IFuseErrnoStats {
  errno_value: number
  name: string
  count: number
}

export interface IFuseStats {
  total_ops: number
  total_successes: number
  total_errors: number
  error_rate: number
  read_bytes: number
  write_bytes: number
  open_handles_current: number
  open_handles_peak: number
  ops: FuseOpStats[]
  top_errors: FuseErrnoStats[]
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

function asCheckedAt(v: unknown): number | string | null {
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.length > 0) return v
  return null
}

function computedRate(errors: number, count: number, fallback: number): number {
  if (Number.isFinite(fallback) && fallback >= 0) return fallback
  return count > 0 ? errors / count : 0
}

export class FuseOpStats implements IFuseOpStats {
  op = 'unknown'
  count = 0
  successes = 0
  errors = 0
  bytes_read = 0
  bytes_written = 0
  total_us = 0
  max_us = 0
  avg_ms = 0
  max_ms = 0
  error_rate = 0

  constructor(data?: Partial<IFuseOpStats>) {
    if (!data) return
    this.op = data.op ?? this.op
    this.count = data.count ?? this.count
    this.successes = data.successes ?? this.successes
    this.errors = data.errors ?? this.errors
    this.bytes_read = data.bytes_read ?? this.bytes_read
    this.bytes_written = data.bytes_written ?? this.bytes_written
    this.total_us = data.total_us ?? this.total_us
    this.max_us = data.max_us ?? this.max_us
    this.avg_ms = data.avg_ms ?? this.avg_ms
    this.max_ms = data.max_ms ?? this.max_ms
    this.error_rate = data.error_rate ?? this.error_rate
  }

  static from(raw: unknown): FuseOpStats {
    const data = asObject(raw)
    const count = asNumber(data.count)
    const errors = asNumber(data.errors)

    return new FuseOpStats({
      op: asString(data.op, 'unknown'),
      count,
      successes: asNumber(data.successes),
      errors,
      bytes_read: asNumber(data.bytes_read),
      bytes_written: asNumber(data.bytes_written),
      total_us: asNumber(data.total_us),
      max_us: asNumber(data.max_us),
      avg_ms: asNumber(data.avg_ms),
      max_ms: asNumber(data.max_ms),
      error_rate: computedRate(errors, count, asNumber(data.error_rate, Number.NaN)),
    })
  }
}

export class FuseErrnoStats implements IFuseErrnoStats {
  errno_value = 0
  name = 'ERRNO_UNKNOWN'
  count = 0

  constructor(data?: Partial<IFuseErrnoStats>) {
    if (!data) return
    this.errno_value = data.errno_value ?? this.errno_value
    this.name = data.name ?? this.name
    this.count = data.count ?? this.count
  }

  static from(raw: unknown): FuseErrnoStats {
    const data = asObject(raw)
    return new FuseErrnoStats({
      errno_value: asNumber(data.errno_value),
      name: asString(data.name, 'ERRNO_UNKNOWN'),
      count: asNumber(data.count),
    })
  }
}

export class FuseStats implements IFuseStats {
  total_ops = 0
  total_successes = 0
  total_errors = 0
  error_rate = 0
  read_bytes = 0
  write_bytes = 0
  open_handles_current = 0
  open_handles_peak = 0
  ops: FuseOpStats[] = []
  top_errors: FuseErrnoStats[] = []
  checked_at: number | string | null = null

  constructor(data?: Partial<IFuseStats>) {
    if (!data) return
    this.total_ops = data.total_ops ?? this.total_ops
    this.total_successes = data.total_successes ?? this.total_successes
    this.total_errors = data.total_errors ?? this.total_errors
    this.error_rate = data.error_rate ?? this.error_rate
    this.read_bytes = data.read_bytes ?? this.read_bytes
    this.write_bytes = data.write_bytes ?? this.write_bytes
    this.open_handles_current = data.open_handles_current ?? this.open_handles_current
    this.open_handles_peak = data.open_handles_peak ?? this.open_handles_peak
    this.ops = data.ops ?? this.ops
    this.top_errors = data.top_errors ?? this.top_errors
    this.checked_at = data.checked_at ?? this.checked_at
  }

  static from(raw: unknown): FuseStats {
    const data = asObject(raw)
    const totalOps = asNumber(data.total_ops)
    const totalErrors = asNumber(data.total_errors)
    const ops = Array.isArray(data.ops) ? data.ops.map(FuseOpStats.from) : []
    const topErrors = Array.isArray(data.top_errors) ? data.top_errors.map(FuseErrnoStats.from) : []

    return new FuseStats({
      total_ops: totalOps,
      total_successes: asNumber(data.total_successes),
      total_errors: totalErrors,
      error_rate: computedRate(totalErrors, totalOps, asNumber(data.error_rate, Number.NaN)),
      read_bytes: asNumber(data.read_bytes),
      write_bytes: asNumber(data.write_bytes),
      open_handles_current: asNumber(data.open_handles_current),
      open_handles_peak: asNumber(data.open_handles_peak),
      ops,
      top_errors: topErrors,
      checked_at: asCheckedAt(data.checked_at),
    })
  }

  totalBytes(): number {
    return this.read_bytes + this.write_bytes
  }

  errorRatePercent(): number {
    return this.error_rate * 100
  }

  topOpsByCount(limit = 5): FuseOpStats[] {
    return [...this.ops].sort((a, b) => b.count - a.count).slice(0, limit)
  }

  topOpsByLatency(limit = 5): FuseOpStats[] {
    return [...this.ops].sort((a, b) => b.max_ms - a.max_ms).slice(0, limit)
  }

  topOpsByErrors(limit = 5): FuseOpStats[] {
    return [...this.ops].sort((a, b) => b.errors - a.errors).slice(0, limit)
  }
}
