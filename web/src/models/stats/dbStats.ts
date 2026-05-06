export type DbHealthStatus = 'healthy' | 'warning' | 'critical' | 'unknown'

export interface IDbTableStats {
  table_name: string
  total_bytes: number
  table_bytes: number
  index_bytes: number
  row_estimate: number
}

export interface IDbStats {
  connected: boolean
  status: DbHealthStatus
  error: string | null
  database_name: string | null
  db_size_bytes: number
  connections_active: number
  connections_idle: number
  connections_idle_in_transaction: number
  connections_total: number
  connections_max: number | null
  cache_hit_ratio: number | null
  pg_stat_statements_enabled: boolean
  slow_query_count: number | null
  deadlocks: number
  temp_bytes: number
  oldest_transaction_age_seconds: number | null
  largest_tables: DbTableStats[]
  checked_at: number | string | null
}

function asObject(v: unknown): Record<string, unknown> {
  return v && typeof v === 'object' && !Array.isArray(v) ? (v as Record<string, unknown>) : {}
}

function asString(v: unknown, fallback = ''): string {
  return typeof v === 'string' ? v : fallback
}

function asNullableString(v: unknown): string | null {
  return typeof v === 'string' && v.length > 0 ? v : null
}

function asNumber(v: unknown, fallback = 0): number {
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.trim() !== '') {
    const parsed = Number(v)
    if (Number.isFinite(parsed)) return parsed
  }
  return fallback
}

function asNullableNumber(v: unknown): number | null {
  if (v == null) return null
  const parsed = asNumber(v, Number.NaN)
  return Number.isFinite(parsed) ? parsed : null
}

function asBoolean(v: unknown, fallback = false): boolean {
  return typeof v === 'boolean' ? v : fallback
}

function asStatus(v: unknown): DbHealthStatus {
  return v === 'healthy' || v === 'warning' || v === 'critical' ? v : 'unknown'
}

function asCheckedAt(v: unknown): number | string | null {
  if (typeof v === 'number' && Number.isFinite(v)) return v
  if (typeof v === 'string' && v.length > 0) return v
  return null
}

export class DbTableStats implements IDbTableStats {
  table_name = 'unknown'
  total_bytes = 0
  table_bytes = 0
  index_bytes = 0
  row_estimate = 0

  constructor(data?: Partial<IDbTableStats>) {
    if (!data) return
    this.table_name = data.table_name ?? this.table_name
    this.total_bytes = data.total_bytes ?? this.total_bytes
    this.table_bytes = data.table_bytes ?? this.table_bytes
    this.index_bytes = data.index_bytes ?? this.index_bytes
    this.row_estimate = data.row_estimate ?? this.row_estimate
  }

  static from(raw: unknown): DbTableStats {
    const data = asObject(raw)
    return new DbTableStats({
      table_name: asString(data.table_name, 'unknown'),
      total_bytes: asNumber(data.total_bytes),
      table_bytes: asNumber(data.table_bytes),
      index_bytes: asNumber(data.index_bytes),
      row_estimate: asNumber(data.row_estimate),
    })
  }
}

export class DbStats implements IDbStats {
  connected = false
  status: DbHealthStatus = 'unknown'
  error: string | null = null
  database_name: string | null = null
  db_size_bytes = 0
  connections_active = 0
  connections_idle = 0
  connections_idle_in_transaction = 0
  connections_total = 0
  connections_max: number | null = null
  cache_hit_ratio: number | null = null
  pg_stat_statements_enabled = false
  slow_query_count: number | null = null
  deadlocks = 0
  temp_bytes = 0
  oldest_transaction_age_seconds: number | null = null
  largest_tables: DbTableStats[] = []
  checked_at: number | string | null = null

  constructor(data?: Partial<IDbStats>) {
    if (!data) return
    this.connected = data.connected ?? this.connected
    this.status = data.status ?? this.status
    this.error = data.error ?? this.error
    this.database_name = data.database_name ?? this.database_name
    this.db_size_bytes = data.db_size_bytes ?? this.db_size_bytes
    this.connections_active = data.connections_active ?? this.connections_active
    this.connections_idle = data.connections_idle ?? this.connections_idle
    this.connections_idle_in_transaction = data.connections_idle_in_transaction ?? this.connections_idle_in_transaction
    this.connections_total = data.connections_total ?? this.connections_total
    this.connections_max = data.connections_max ?? this.connections_max
    this.cache_hit_ratio = data.cache_hit_ratio ?? this.cache_hit_ratio
    this.pg_stat_statements_enabled = data.pg_stat_statements_enabled ?? this.pg_stat_statements_enabled
    this.slow_query_count = data.slow_query_count ?? this.slow_query_count
    this.deadlocks = data.deadlocks ?? this.deadlocks
    this.temp_bytes = data.temp_bytes ?? this.temp_bytes
    this.oldest_transaction_age_seconds = data.oldest_transaction_age_seconds ?? this.oldest_transaction_age_seconds
    this.largest_tables = data.largest_tables ?? this.largest_tables
    this.checked_at = data.checked_at ?? this.checked_at
  }

  static from(raw: unknown): DbStats {
    const data = asObject(raw)
    const largestTables = Array.isArray(data.largest_tables) ? data.largest_tables.map(DbTableStats.from) : []

    return new DbStats({
      connected: asBoolean(data.connected),
      status: asStatus(data.status),
      error: asNullableString(data.error),
      database_name: asNullableString(data.database_name),
      db_size_bytes: asNumber(data.db_size_bytes),
      connections_active: asNumber(data.connections_active),
      connections_idle: asNumber(data.connections_idle),
      connections_idle_in_transaction: asNumber(data.connections_idle_in_transaction),
      connections_total: asNumber(data.connections_total),
      connections_max: asNullableNumber(data.connections_max),
      cache_hit_ratio: asNullableNumber(data.cache_hit_ratio),
      pg_stat_statements_enabled: asBoolean(data.pg_stat_statements_enabled),
      slow_query_count: asNullableNumber(data.slow_query_count),
      deadlocks: asNumber(data.deadlocks),
      temp_bytes: asNumber(data.temp_bytes),
      oldest_transaction_age_seconds: asNullableNumber(data.oldest_transaction_age_seconds),
      largest_tables: largestTables,
      checked_at: asCheckedAt(data.checked_at),
    })
  }

  connectionUsage(): number | null {
    if (!this.connections_max || this.connections_max <= 0) return null
    return this.connections_total / this.connections_max
  }
}
