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

function asString(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback
}

function asNullableString(value: unknown): string | null {
  return typeof value === 'string' && value.length > 0 ? value : null
}

function asNullableNumber(value: unknown): number | null {
  if (value == null) return null
  const parsed = asNumber(value, Number.NaN)
  return Number.isFinite(parsed) ? parsed : null
}

function asArray(value: unknown): unknown[] {
  return Array.isArray(value) ? value : []
}

export class VaultActivityTopUser {
  user_id: number | null
  user_name: string | null
  count: number

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.user_id = asNullableNumber(data.user_id)
    this.user_name = asNullableString(data.user_name)
    this.count = asNumber(data.count)
  }

  static from(input: unknown): VaultActivityTopUser {
    return new VaultActivityTopUser(input)
  }

  label(): string {
    return this.user_name ?? (this.user_id == null ? 'unknown' : `uid ${this.user_id}`)
  }
}

export class VaultActivityTopPath {
  path: string
  action: string
  count: number
  bytes: number

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.path = asString(data.path, '/')
    this.action = asString(data.action, 'unknown')
    this.count = asNumber(data.count)
    this.bytes = asNumber(data.bytes)
  }

  static from(input: unknown): VaultActivityTopPath {
    return new VaultActivityTopPath(input)
  }
}

export class VaultActivityEvent {
  source: string
  action: string
  path: string
  user_id: number | null
  user_name: string | null
  status: string | null
  error: string | null
  bytes: number
  occurred_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.source = asString(data.source, 'unknown')
    this.action = asString(data.action, 'unknown')
    this.path = asString(data.path, '/')
    this.user_id = asNullableNumber(data.user_id)
    this.user_name = asNullableString(data.user_name)
    this.status = asNullableString(data.status)
    this.error = asNullableString(data.error)
    this.bytes = asNumber(data.bytes)
    this.occurred_at =
      typeof data.occurred_at === 'string' ? data.occurred_at : asNullableNumber(data.occurred_at)
  }

  static from(input: unknown): VaultActivityEvent {
    return new VaultActivityEvent(input)
  }

  occurredAtMs(): number | null {
    if (typeof this.occurred_at === 'number') {
      if (!Number.isFinite(this.occurred_at) || this.occurred_at <= 0) return null
      return this.occurred_at > 1_000_000_000_000 ? this.occurred_at : this.occurred_at * 1000
    }
    if (typeof this.occurred_at === 'string') {
      const parsed = Date.parse(this.occurred_at)
      return Number.isFinite(parsed) ? parsed : null
    }
    return null
  }
}

export class VaultActivity {
  vault_id: number
  last_activity_at: number | string | null
  last_activity_action: string | null
  uploads_24h: number
  uploads_7d: number
  deletes_24h: number
  deletes_7d: number
  renames_24h: number
  renames_7d: number
  moves_24h: number
  moves_7d: number
  copies_24h: number
  copies_7d: number
  restores_24h: number
  restores_7d: number
  bytes_added_24h: number
  bytes_removed_24h: number
  top_active_users: VaultActivityTopUser[]
  top_touched_paths: VaultActivityTopPath[]
  recent_activity: VaultActivityEvent[]
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNumber(data.vault_id)
    this.last_activity_at =
      typeof data.last_activity_at === 'string' ? data.last_activity_at : asNullableNumber(data.last_activity_at)
    this.last_activity_action = asNullableString(data.last_activity_action)
    this.uploads_24h = asNumber(data.uploads_24h)
    this.uploads_7d = asNumber(data.uploads_7d)
    this.deletes_24h = asNumber(data.deletes_24h)
    this.deletes_7d = asNumber(data.deletes_7d)
    this.renames_24h = asNumber(data.renames_24h)
    this.renames_7d = asNumber(data.renames_7d)
    this.moves_24h = asNumber(data.moves_24h)
    this.moves_7d = asNumber(data.moves_7d)
    this.copies_24h = asNumber(data.copies_24h)
    this.copies_7d = asNumber(data.copies_7d)
    this.restores_24h = asNumber(data.restores_24h)
    this.restores_7d = asNumber(data.restores_7d)
    this.bytes_added_24h = asNumber(data.bytes_added_24h)
    this.bytes_removed_24h = asNumber(data.bytes_removed_24h)
    this.top_active_users = asArray(data.top_active_users).map(VaultActivityTopUser.from)
    this.top_touched_paths = asArray(data.top_touched_paths).map(VaultActivityTopPath.from)
    this.recent_activity = asArray(data.recent_activity).map(VaultActivityEvent.from)
    this.checked_at = typeof data.checked_at === 'string' ? data.checked_at : asNullableNumber(data.checked_at)
  }

  static from(input: unknown): VaultActivity {
    return new VaultActivity(input)
  }

  totalMutations24h(): number {
    return this.uploads_24h + this.deletes_24h + this.renames_24h + this.moves_24h + this.copies_24h + this.restores_24h
  }

  hasActivity(): boolean {
    return this.totalMutations24h() > 0 || this.recent_activity.length > 0 || this.last_activity_at != null
  }
}
