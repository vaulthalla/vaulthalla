// Mirrors `vh::config::to_json(Config)` in core/src/config/Config.cpp. The daemon never sends secrets here
// (the DB password is TPM-sealed and the JWT secret lives in the secrets manager), so the web never renders,
// defaults, or sends them. `settings.update` merges a partial payload onto the current config server-side.

export type SettingsValue = string | number | boolean | null | SettingsValue[] | SettingsSection

export interface SettingsSection {
  [key: string]: SettingsValue
}

export interface WebsocketServerSettings extends SettingsSection {
  enabled: boolean
  host: string
  port: number
  max_connections: number
  max_upload_size_bytes: number
}

export interface HttpPreviewServerSettings extends SettingsSection {
  enabled: boolean
  host: string
  port: number
  max_connections: number
  max_preview_size_bytes: number
}

export interface DatabaseSettings extends SettingsSection {
  host: string
  port: number
  name: string
  user: string
  pool_size: number
}

export interface AuthSettings extends SettingsSection {
  access_token_expiry_minutes: number
  refresh_token_expiry_days: number
}

export interface Settings {
  websocket_server: WebsocketServerSettings
  http_preview_server: HttpPreviewServerSettings
  s3_gateway: SettingsSection
  caching: SettingsSection
  database: DatabaseSettings
  auth: AuthSettings
  sync: SettingsSection
  pricing: SettingsSection
  services: SettingsSection
  stats_snapshots: SettingsSection
  sharing: SettingsSection
  email: SettingsSection
  operator_emails: SettingsSection
  auditing: SettingsSection
  // Older daemons have no vaults section.
  vaults?: SettingsSection
  logging: SettingsSection
  dev: SettingsSection
}

export const isSettingsSection = (value: SettingsValue | undefined): value is SettingsSection =>
  typeof value === 'object' && value !== null && !Array.isArray(value)
