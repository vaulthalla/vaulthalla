export type StatsTrendScope = 'system' | 'vault'
export type StatsTrendUnit = 'bytes' | 'bytes/s' | 'ratio' | 'count' | 'ops/s' | 'ms' | 'seconds' | 'unknown'

export interface IStatsTrendPoint {
  created_at: number | string | null
  value: number
}

export interface IStatsTrendSeries {
  key: string
  label: string
  unit: StatsTrendUnit
  snapshot_type: string
  points: StatsTrendPoint[]
}

export interface IStatsTrends {
  scope: StatsTrendScope
  vault_id: number | null
  window_hours: number
  series: StatsTrendSeries[]
  checked_at: number | string | null
}

type UnknownRecord = Record<string, unknown>

function isRecord(value: unknown): value is UnknownRecord {
  return !!value && typeof value === 'object' && !Array.isArray(value)
}

function asString(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback
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

function asCheckedAt(value: unknown): number | string | null {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'string' && value.length > 0) return value
  return null
}

function asScope(value: unknown): StatsTrendScope {
  return value === 'vault' ? 'vault' : 'system'
}

function asUnit(value: unknown): StatsTrendUnit {
  return value === 'bytes' || value === 'bytes/s' || value === 'ratio' || value === 'count' || value === 'ops/s' || value === 'ms' || value === 'seconds' ? value : 'unknown'
}

function asArray(value: unknown): unknown[] {
  return Array.isArray(value) ? value : []
}

export class StatsTrendPoint implements IStatsTrendPoint {
  created_at: number | string | null = null
  value = 0

  constructor(data?: Partial<IStatsTrendPoint>) {
    if (!data) return
    this.created_at = data.created_at ?? this.created_at
    this.value = data.value ?? this.value
  }

  static from(raw: unknown): StatsTrendPoint {
    const data = isRecord(raw) ? raw : {}
    return new StatsTrendPoint({
      created_at: asCheckedAt(data.created_at),
      value: asNumber(data.value),
    })
  }

  createdAtMs(): number | null {
    if (typeof this.created_at === 'number') {
      if (!Number.isFinite(this.created_at) || this.created_at <= 0) return null
      return this.created_at > 1_000_000_000_000 ? this.created_at : this.created_at * 1000
    }

    if (typeof this.created_at === 'string') {
      const parsed = Date.parse(this.created_at)
      return Number.isFinite(parsed) ? parsed : null
    }

    return null
  }
}

export class StatsTrendSeries implements IStatsTrendSeries {
  key = 'unknown'
  label = 'Unknown'
  unit: StatsTrendUnit = 'unknown'
  snapshot_type = 'unknown'
  points: StatsTrendPoint[] = []

  constructor(data?: Partial<IStatsTrendSeries>) {
    if (!data) return
    this.key = data.key ?? this.key
    this.label = data.label ?? this.label
    this.unit = data.unit ?? this.unit
    this.snapshot_type = data.snapshot_type ?? this.snapshot_type
    this.points = data.points ?? this.points
  }

  static from(raw: unknown): StatsTrendSeries {
    const data = isRecord(raw) ? raw : {}
    return new StatsTrendSeries({
      key: asString(data.key, 'unknown'),
      label: asString(data.label, 'Unknown'),
      unit: asUnit(data.unit),
      snapshot_type: asString(data.snapshot_type, 'unknown'),
      points: asArray(data.points).map(StatsTrendPoint.from),
    })
  }

  latest(): StatsTrendPoint | null {
    return this.points.length ? this.points[this.points.length - 1] : null
  }

  firstSince(hours: number): StatsTrendPoint | null {
    const cutoff = Date.now() - hours * 60 * 60 * 1000
    return this.points.find(point => {
      const ms = point.createdAtMs()
      return ms != null && ms >= cutoff
    }) ?? this.points[0] ?? null
  }

  deltaSince(hours: number): number | null {
    const latest = this.latest()
    const baseline = this.firstSince(hours)
    if (!latest || !baseline) return null
    return latest.value - baseline.value
  }

  hasData(): boolean {
    return this.points.length > 0
  }
}

export class StatsTrends implements IStatsTrends {
  scope: StatsTrendScope = 'system'
  vault_id: number | null = null
  window_hours = 168
  series: StatsTrendSeries[] = []
  checked_at: number | string | null = null

  constructor(data?: Partial<IStatsTrends>) {
    if (!data) return
    this.scope = data.scope ?? this.scope
    this.vault_id = data.vault_id ?? this.vault_id
    this.window_hours = data.window_hours ?? this.window_hours
    this.series = data.series ?? this.series
    this.checked_at = data.checked_at ?? this.checked_at
  }

  static from(raw: unknown): StatsTrends {
    const data = isRecord(raw) ? raw : {}
    return new StatsTrends({
      scope: asScope(data.scope),
      vault_id: asNullableNumber(data.vault_id),
      window_hours: asNumber(data.window_hours, 168),
      series: asArray(data.series).map(StatsTrendSeries.from),
      checked_at: asCheckedAt(data.checked_at),
    })
  }

  hasData(): boolean {
    return this.series.some(series => series.hasData())
  }

  checkedAtMs(): number | null {
    if (typeof this.checked_at === 'number') return this.checked_at > 1_000_000_000_000 ? this.checked_at : this.checked_at * 1000
    if (typeof this.checked_at === 'string') {
      const parsed = Date.parse(this.checked_at)
      return Number.isFinite(parsed) ? parsed : null
    }
    return null
  }
}
