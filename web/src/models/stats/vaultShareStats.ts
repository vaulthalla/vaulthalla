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

export class VaultShareTopLink {
  share_id: string
  label: string
  root_path: string
  link_type: string
  access_mode: string
  access_count: number
  download_count: number
  upload_count: number
  last_accessed_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.share_id = asString(data.share_id)
    this.label = asString(data.label, 'share link')
    this.root_path = asString(data.root_path, '/')
    this.link_type = asString(data.link_type, 'unknown')
    this.access_mode = asString(data.access_mode, 'unknown')
    this.access_count = asNumber(data.access_count)
    this.download_count = asNumber(data.download_count)
    this.upload_count = asNumber(data.upload_count)
    this.last_accessed_at =
      typeof data.last_accessed_at === 'string' ? data.last_accessed_at : asNullableNumber(data.last_accessed_at)
  }

  static from(input: unknown): VaultShareTopLink {
    return new VaultShareTopLink(input)
  }
}

export class VaultShareRecentEvent {
  id: number
  share_id: string | null
  event_type: string
  status: string
  target_path: string | null
  bytes_transferred: number
  error_code: string | null
  error_message: string | null
  created_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.id = asNumber(data.id)
    this.share_id = asNullableString(data.share_id)
    this.event_type = asString(data.event_type, 'unknown')
    this.status = asString(data.status, 'unknown')
    this.target_path = asNullableString(data.target_path)
    this.bytes_transferred = asNumber(data.bytes_transferred)
    this.error_code = asNullableString(data.error_code)
    this.error_message = asNullableString(data.error_message)
    this.created_at = typeof data.created_at === 'string' ? data.created_at : asNullableNumber(data.created_at)
  }

  static from(input: unknown): VaultShareRecentEvent {
    return new VaultShareRecentEvent(input)
  }

  hasProblem(): boolean {
    return this.status === 'denied' || this.status === 'failed' || this.status === 'rate_limited' || !!this.error_message
  }
}

export class VaultShareStats {
  vault_id: number
  active_links: number
  expired_links: number
  revoked_links: number
  links_created_24h: number
  links_revoked_24h: number
  public_links: number
  email_validated_links: number
  downloads_24h: number
  uploads_24h: number
  denied_attempts_24h: number
  rate_limited_attempts_24h: number
  failed_attempts_24h: number
  top_links_by_access: VaultShareTopLink[]
  recent_share_events: VaultShareRecentEvent[]
  checked_at: number | string | null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.vault_id = asNumber(data.vault_id)
    this.active_links = asNumber(data.active_links)
    this.expired_links = asNumber(data.expired_links)
    this.revoked_links = asNumber(data.revoked_links)
    this.links_created_24h = asNumber(data.links_created_24h)
    this.links_revoked_24h = asNumber(data.links_revoked_24h)
    this.public_links = asNumber(data.public_links)
    this.email_validated_links = asNumber(data.email_validated_links)
    this.downloads_24h = asNumber(data.downloads_24h)
    this.uploads_24h = asNumber(data.uploads_24h)
    this.denied_attempts_24h = asNumber(data.denied_attempts_24h)
    this.rate_limited_attempts_24h = asNumber(data.rate_limited_attempts_24h)
    this.failed_attempts_24h = asNumber(data.failed_attempts_24h)
    this.top_links_by_access = asArray(data.top_links_by_access).map(VaultShareTopLink.from)
    this.recent_share_events = asArray(data.recent_share_events).map(VaultShareRecentEvent.from)
    this.checked_at = typeof data.checked_at === 'string' ? data.checked_at : asNullableNumber(data.checked_at)
  }

  static from(input: unknown): VaultShareStats {
    return new VaultShareStats(input)
  }

  activeExposure(): number {
    return this.public_links + this.email_validated_links
  }

  problemAttempts24h(): number {
    return this.denied_attempts_24h + this.rate_limited_attempts_24h + this.failed_attempts_24h
  }

  hasProblems(): boolean {
    return this.problemAttempts24h() > 0 || this.expired_links > 0 || this.revoked_links > 0
  }
}
