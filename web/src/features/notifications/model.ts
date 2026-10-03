// Operator email (email.* commands). Mirrors core's EmailConfig / OperatorEmailsConfig JSON.
import { bool, num, rec, str } from '@/features/cost/model'

export type Provider = 'none' | 'resend' | 'ses'
export type Group = 'alerts' | 'weekly' | 'security'
export const GROUPS: { key: Group; label: string; hint: string }[] = [
  { key: 'alerts', label: 'Alerts', hint: 'Health and budget alerts' },
  { key: 'weekly', label: 'Weekly recap', hint: 'The weekly digest' },
  { key: 'security', label: 'Security', hint: 'Admin role changes and other security notices' },
]
export const WEEKDAYS = ['monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday', 'sunday']
export const SEVERITIES = ['info', 'warning', 'critical'] as const
export type Severity = (typeof SEVERITIES)[number]

export interface EmailSettings {
  email: {
    enabled: boolean
    provider: Provider
    from: string
    reply_to: string | null
    base_url: string | null
    resend: { endpoint: string }
    ses: { region: string; endpoint: string | null }
  }
  operator: {
    enabled: boolean
    recipients: Record<Group, string[]>
    alerting: {
      enabled: boolean
      min_severity: Severity
      dedupe_window_minutes: number
      repeat_after_hours: number
      send_recovery: boolean
      health_poll_seconds: number
    }
    weekly_digest: { enabled: boolean; weekday: string; hour_local: number; timezone: string }
    security_alerts: { enabled: boolean; admin_role_changes: boolean }
  }
  secrets: {
    available: boolean | null
    resend_api_key: boolean | null
    ses_access_key_id: boolean | null
    ses_secret_access_key: boolean | null
  }
}

const list = (v: unknown) => (Array.isArray(v) ? v.map(str).filter((x): x is string => x !== null) : [])
const provider = (v: unknown): Provider => (v === 'resend' || v === 'ses' ? v : 'none')

export const toSettings = (input: unknown): EmailSettings => {
  const d = rec(input)
  const e = rec(d.email)
  const o = rec(d.operator_emails)
  const r = rec(o.recipients)
  const a = rec(o.alerting)
  const w = rec(o.weekly_digest)
  const s = rec(o.security_alerts)
  const sec = rec(d.secrets)
  return {
    email: {
      enabled: bool(e.enabled) ?? false,
      provider: provider(e.provider),
      from: str(e.from) ?? '',
      reply_to: str(e.reply_to),
      base_url: str(e.base_url),
      resend: { endpoint: str(rec(e.resend).endpoint) ?? '' },
      ses: { region: str(rec(e.ses).region) ?? '', endpoint: str(rec(e.ses).endpoint) },
    },
    operator: {
      enabled: bool(o.enabled) ?? true,
      recipients: { alerts: list(r.alerts), weekly: list(r.weekly), security: list(r.security) },
      alerting: {
        enabled: bool(a.enabled) ?? true,
        min_severity: SEVERITIES.find(s => s === str(a.min_severity)?.toLowerCase()) ?? 'warning',
        dedupe_window_minutes: num(a.dedupe_window_minutes) ?? 60,
        repeat_after_hours: num(a.repeat_after_hours) ?? 24,
        send_recovery: bool(a.send_recovery) ?? true,
        health_poll_seconds: num(a.health_poll_seconds) ?? 60,
      },
      weekly_digest: {
        enabled: bool(w.enabled) ?? true,
        weekday: str(w.weekday) ?? 'monday',
        hour_local: num(w.hour_local) ?? 8,
        timezone: str(w.timezone) ?? 'UTC',
      },
      security_alerts: { enabled: bool(s.enabled) ?? true, admin_role_changes: bool(s.admin_role_changes) ?? true },
    },
    secrets: {
      available: bool(sec.available),
      resend_api_key: bool(sec.resend_api_key),
      ses_access_key_id: bool(sec.ses_access_key_id),
      ses_secret_access_key: bool(sec.ses_secret_access_key),
    },
  }
}

// "Vaulthalla <ops@example.com>" ⇄ { name, address }
export const splitFrom = (from: string) => {
  const m = from.match(/^\s*(.*?)\s*<([^>]+)>\s*$/)
  return m ? { name: m[1].replace(/^"|"$/g, ''), address: m[2].trim() } : { name: '', address: from.trim() }
}
export const joinFrom = (name: string, address: string) =>
  name.trim() ? `${name.trim()} <${address.trim()}>` : address.trim()

export interface HistoryRecord {
  id: number
  event_type: string | null
  severity: string | null
  provider: string | null
  subject: string | null
  recipient_group: string | null
  recipient_count: number | null
  status: string | null
  error_summary: string | null
  sent_at: string | null
  created_at: string | null
}

export const toHistory = (input: unknown): HistoryRecord => {
  const d = rec(input)
  return {
    id: num(d.id) ?? 0,
    event_type: str(d.event_type),
    severity: str(d.severity),
    provider: str(d.provider),
    subject: str(d.subject),
    recipient_group: str(d.recipient_group),
    recipient_count: num(d.recipient_count),
    status: str(d.status),
    error_summary: str(d.error_summary),
    sent_at: str(d.sent_at),
    created_at: str(d.created_at),
  }
}
