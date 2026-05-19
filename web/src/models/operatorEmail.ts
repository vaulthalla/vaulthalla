export type EmailProvider = 'none' | 'resend' | 'ses'
export type RecipientGroup = 'alerts' | 'weekly' | 'security'

export interface ResendEmailConfig {
  endpoint: string
}

export interface SesEmailConfig {
  region: string
  endpoint: string | null
}

export interface EmailConfig {
  enabled: boolean
  provider: EmailProvider
  from: string
  reply_to: string | null
  base_url: string | null
  resend: ResendEmailConfig
  ses: SesEmailConfig
}

export interface OperatorEmailRecipientsConfig {
  alerts: string[]
  weekly: string[]
  security: string[]
}

export interface OperatorEmailAlertingConfig {
  enabled: boolean
  min_severity: 'info' | 'warning' | 'critical'
  dedupe_window_minutes: number
  repeat_after_hours: number
  send_recovery: boolean
  health_poll_seconds: number
}

export interface OperatorEmailDigestConfig {
  enabled: boolean
  weekday: string
  hour_local: number
  timezone: string
}

export interface OperatorEmailSecurityAlertsConfig {
  enabled: boolean
  admin_role_changes: boolean
}

export interface OperatorEmailsConfig {
  enabled: boolean
  recipients: OperatorEmailRecipientsConfig
  alerting: OperatorEmailAlertingConfig
  weekly_digest: OperatorEmailDigestConfig
  security_alerts: OperatorEmailSecurityAlertsConfig
}

export interface EmailSecretStatus {
  available: boolean
  resend_api_key: boolean
  ses_access_key_id: boolean
  ses_secret_access_key: boolean
}

export interface OperatorEmailConfigResponse {
  email: EmailConfig
  operator_emails: OperatorEmailsConfig
  secrets: EmailSecretStatus
}

export type EmailConfigPatch = Partial<Omit<EmailConfig, 'resend' | 'ses'>> & {
  resend?: Partial<ResendEmailConfig>
  ses?: Partial<SesEmailConfig>
}

export type OperatorEmailsConfigPatch = Partial<
  Omit<OperatorEmailsConfig, 'recipients' | 'alerting' | 'weekly_digest' | 'security_alerts'>
> & {
  recipients?: Partial<OperatorEmailRecipientsConfig>
  alerting?: Partial<OperatorEmailAlertingConfig>
  weekly_digest?: Partial<OperatorEmailDigestConfig>
  security_alerts?: Partial<OperatorEmailSecurityAlertsConfig>
}

export interface OperatorEmailConfigPatch {
  email?: EmailConfigPatch
  operator_emails?: OperatorEmailsConfigPatch
}

export interface OperatorEmailSecretPayload {
  provider: Extract<EmailProvider, 'resend' | 'ses'>
  api_key?: string
  access_key_id?: string
  secret_access_key?: string
}

export interface OperatorEmailTestPayload {
  to: string
  dry_run?: boolean
}

export interface OperatorEmailTestResponse {
  status: 'sent' | 'dry_run'
  provider?: string
  to: string
  subject: string
  provider_message_id?: string | null
  html_bytes?: number
  text?: string
  history_warning?: string | null
}

export interface OperatorEmailHistoryRecord {
  id: number
  event_key: string
  event_type: string
  severity: string
  provider: string
  subject: string
  recipient_group: string | null
  recipient_count: number
  provider_message_id: string | null
  status: string
  error_summary: string | null
  fingerprint: string
  first_seen_at: string
  last_seen_at: string
  sent_at: string | null
  created_at: string
}
