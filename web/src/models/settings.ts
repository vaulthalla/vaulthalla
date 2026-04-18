export interface ServerConfig {
  host: string
  port: number
  uds_socket: string
  log_level: 'debug' | 'info' | 'warn' | 'error'
  max_connections: number
}

export interface FuseConfig {
  enabled: boolean
  root_mount_path: string
  mount_per_user: boolean
  fuse_timeout_seconds: number
  allow_other: boolean
}

export interface DatabaseConfig {
  host: string
  port: number
  name: string
  user: string
  password: string
  pool_size: number
}

export interface AuthConfig {
  token_expiry_minutes: number
  refresh_token_expiry_days: number
  jwt_secret: string
  allow_signup: boolean
}

export interface MetricsConfig {
  enabled: boolean
  port: number
}

export interface AdminUIConfig {
  enabled: boolean
  bind_port: number
  allowed_ips: string[]
}

export interface SchedulerConfig {
  cleanup_interval_hours: number
  audit_prune_days: number
  usage_refresh_minutes: number
}

export interface AdvancedConfig {
  enable_file_versioning: boolean
  max_upload_size_mb: number
  enable_sharing: boolean
  enable_public_links: boolean
  rate_limit_per_ip_per_minute: number
}

export interface VaulthallaConfig {
  server: ServerConfig
  fuse: FuseConfig
  database: DatabaseConfig
  auth: AuthConfig
  metrics: MetricsConfig
  admin_ui: AdminUIConfig
  scheduler: SchedulerConfig
  advanced: AdvancedConfig
}

export class Settings implements VaulthallaConfig {
  server: ServerConfig
  fuse: FuseConfig
  database: DatabaseConfig
  auth: AuthConfig
  metrics: MetricsConfig
  admin_ui: AdminUIConfig
  scheduler: SchedulerConfig
  advanced: AdvancedConfig

  constructor(config: VaulthallaConfig) {
    this.server = config.server
    this.fuse = config.fuse
    this.database = config.database
    this.auth = config.auth
    this.metrics = config.metrics
    this.admin_ui = config.admin_ui
    this.scheduler = config.scheduler
    this.advanced = config.advanced
  }

  static fromData(data: Partial<VaulthallaConfig>): Settings {
    const defaultConfig: VaulthallaConfig = {
      server: {
        host: '0.0.0.0',
        port: 36969,
        uds_socket: '/tmp/vaulthalla.sock',
        log_level: 'info',
        max_connections: 1024,
      },
      fuse: {
        enabled: true,
        root_mount_path: '/mnt/vaulthalla',
        mount_per_user: true,
        fuse_timeout_seconds: 60,
        allow_other: true,
      },
      database: {
        host: 'localhost',
        port: 5432,
        name: 'vaulthalla',
        user: 'vaulthalla',
        password: 'changeme',
        pool_size: 10,
      },
      auth: {
        token_expiry_minutes: 60,
        refresh_token_expiry_days: 7,
        jwt_secret: 'changeme-very-secret',
        allow_signup: false,
      },
      metrics: { enabled: true, port: 9100 },
      admin_ui: { enabled: true, bind_port: 9090, allowed_ips: ['127.0.0.1', '::1'] },
      scheduler: { cleanup_interval_hours: 24, audit_prune_days: 90, usage_refresh_minutes: 10 },
      advanced: {
        enable_file_versioning: true,
        max_upload_size_mb: 2048,
        enable_sharing: true,
        enable_public_links: true,
        rate_limit_per_ip_per_minute: 60,
      },
    }

    return new Settings({
      server: { ...defaultConfig.server, ...data.server },
      fuse: { ...defaultConfig.fuse, ...data.fuse },
      database: { ...defaultConfig.database, ...data.database },
      auth: { ...defaultConfig.auth, ...data.auth },
      metrics: { ...defaultConfig.metrics, ...data.metrics },
      admin_ui: { ...defaultConfig.admin_ui, ...data.admin_ui },
      scheduler: { ...defaultConfig.scheduler, ...data.scheduler },
      advanced: { ...defaultConfig.advanced, ...data.advanced },
    })
  }
}
