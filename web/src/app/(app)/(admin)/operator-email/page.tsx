'use client'

import React, { useEffect, useState } from 'react'
import EnvelopeIcon from '@/fa-duotone/envelope.svg'
import CheckIcon from '@/fa-duotone/circle-check.svg'
import EditIcon from '@/fa-duotone/file-pen.svg'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import SendIcon from '@/fa-duotone/paper-plane.svg'
import PlusIcon from '@/fa-duotone/circle-plus.svg'
import TrashIcon from '@/fa-duotone/circle-trash.svg'
import RefreshIcon from '@/fa-duotone/arrows-rotate.svg'
import ShieldIcon from '@/fa-duotone/shield-keyhole.svg'
import CalendarIcon from '@/fa-duotone/calendar-clock.svg'
import BellIcon from '@/fa-duotone/bell-on.svg'
import { AdminPage } from '@/components/admin/AdminPage'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import {
  EmailConfig,
  EmailProvider,
  OperatorEmailConfigPatch,
  OperatorEmailRecipientsConfig,
  OperatorEmailsConfig,
  RecipientGroup,
} from '@/models/operatorEmail'
import { useOperatorEmailStore } from '@/stores/operatorEmailStore'

const DEFAULT_DISPLAY_NAME = 'Vaulthalla'
const weekdays = ['sunday', 'monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday']
const recipientGroups: RecipientGroup[] = ['alerts', 'weekly', 'security']

type SenderFields = {
  displayName: string
  senderId: string
  senderDomain: string
  replyTo: string
  baseUrl: string
}

type OperatorRecipientRow = {
  email: string
  alerts: boolean
  weekly: boolean
  security: boolean
}

const cloneEmail = (email: EmailConfig): EmailConfig => ({
  ...email,
  resend: { ...email.resend },
  ses: { ...email.ses },
})

const cloneOperator = (operatorEmails: OperatorEmailsConfig): OperatorEmailsConfig => ({
  ...operatorEmails,
  recipients: {
    alerts: [...operatorEmails.recipients.alerts],
    weekly: [...operatorEmails.recipients.weekly],
    security: [...operatorEmails.recipients.security],
  },
  alerting: { ...operatorEmails.alerting },
  weekly_digest: { ...operatorEmails.weekly_digest },
  security_alerts: { ...operatorEmails.security_alerts },
})

const parseSender = (email: EmailConfig): SenderFields => {
  const raw = email.from.trim()
  const match = raw.match(/^\s*(.*?)\s*<([^>]+)>\s*$/)
  const displayName = match?.[1]?.trim() || DEFAULT_DISPLAY_NAME
  const address = (match?.[2] || raw).trim()
  const at = address.indexOf('@')

  return {
    displayName,
    senderId: at >= 0 ? address.slice(0, at) : address,
    senderDomain: at >= 0 ? address.slice(at + 1) : '',
    replyTo: email.reply_to || '',
    baseUrl: email.base_url || '',
  }
}

const buildSenderAddress = (sender: SenderFields) => {
  const senderId = sender.senderId.trim()
  if (!senderId) return null

  const email = senderId.includes('@')
    ? senderId
    : sender.senderDomain.trim().replace(/^@/, '')
      ? `${senderId}@${sender.senderDomain.trim().replace(/^@/, '')}`
      : ''

  if (!email) return null
  const displayName = sender.displayName.trim()
  return displayName ? `${displayName} <${email}>` : email
}

const rowsFromRecipients = (recipients: OperatorEmailRecipientsConfig): OperatorRecipientRow[] => {
  const rows = new Map<string, OperatorRecipientRow>()

  recipientGroups.forEach(group => {
    recipients[group].forEach(email => {
      const row = rows.get(email) || { email, alerts: false, weekly: false, security: false }
      row[group] = true
      rows.set(email, row)
    })
  })

  return Array.from(rows.values()).sort((a, b) => a.email.localeCompare(b.email))
}

const recipientsFromRows = (rows: OperatorRecipientRow[]): OperatorEmailRecipientsConfig => ({
  alerts: rows.filter(row => row.alerts).map(row => row.email),
  weekly: rows.filter(row => row.weekly).map(row => row.email),
  security: rows.filter(row => row.security).map(row => row.email),
})

const preferredRecipient = (rows: OperatorRecipientRow[]) => {
  for (const group of recipientGroups) {
    const row = rows.find(candidate => candidate[group])
    if (row) return row.email
  }
  return ''
}

const Section = ({
  title,
  icon: Icon,
  children,
}: {
  title: string
  icon: React.ComponentType<React.SVGProps<SVGSVGElement>>
  children: React.ReactNode
}) => (
  <section className="rounded border border-white/10 bg-gray-900 text-white">
    <div className="flex items-center gap-2 border-b border-gray-800 px-4 py-3">
      <Icon className="h-4 w-4 fill-current text-cyan-300" />
      <h2 className="text-sm font-semibold uppercase tracking-normal text-gray-200">{title}</h2>
    </div>
    <div className="space-y-4 p-4">{children}</div>
  </section>
)

const TextField = ({
  label,
  value,
  onChange,
  type = 'text',
  placeholder,
}: {
  label: string
  value: string
  onChange: (value: string) => void
  type?: string
  placeholder?: string
}) => (
  <label className="flex flex-col gap-1 text-sm text-gray-300">
    {label}
    <input
      className="min-h-10 rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white outline-none focus:border-cyan-400"
      placeholder={placeholder}
      type={type}
      value={value}
      onChange={event => onChange(event.target.value)}
    />
  </label>
)

const NumberField = ({
  label,
  value,
  min,
  max,
  onChange,
}: {
  label: string
  value: number
  min?: number
  max?: number
  onChange: (value: number) => void
}) => (
  <label className="flex flex-col gap-1 text-sm text-gray-300">
    {label}
    <input
      className="min-h-10 rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white outline-none focus:border-cyan-400"
      type="number"
      min={min}
      max={max}
      value={value}
      onChange={event => onChange(Number(event.target.value))}
    />
  </label>
)

const ToggleField = ({ label, checked, onChange }: { label: string; checked: boolean; onChange: (value: boolean) => void }) => (
  <label className="flex min-h-10 items-center justify-between gap-3 rounded border border-gray-800 bg-gray-950 px-3 py-2 text-sm text-gray-200">
    <span>{label}</span>
    <input className="h-4 w-4 accent-cyan-400" type="checkbox" checked={checked} onChange={event => onChange(event.target.checked)} />
  </label>
)

const ActionButton = ({
  children,
  icon: Icon,
  onClick,
  disabled,
}: {
  children: React.ReactNode
  icon: React.ComponentType<React.SVGProps<SVGSVGElement>>
  onClick: () => void
  disabled?: boolean
}) => (
  <button
    className="inline-flex min-h-10 items-center justify-center gap-2 rounded border border-cyan-500/40 bg-cyan-500/10 px-3 py-2 text-sm text-cyan-100 hover:bg-cyan-500/20 disabled:cursor-not-allowed disabled:opacity-50"
    disabled={disabled}
    onClick={onClick}
    type="button">
    <Icon className="h-4 w-4 fill-current" />
    {children}
  </button>
)

const ProviderSegment = ({ value, onChange }: { value: EmailProvider; onChange: (provider: EmailProvider) => void }) => (
  <div className="grid grid-cols-3 rounded border border-gray-700 bg-gray-950 p-1 text-sm">
    {(['none', 'resend', 'ses'] as EmailProvider[]).map(provider => (
      <button
        key={provider}
        className={`rounded px-3 py-2 uppercase tracking-normal ${value === provider ? 'bg-cyan-400 text-gray-950' : 'text-gray-300 hover:bg-white/10'}`}
        onClick={() => onChange(provider)}
        type="button">
        {provider}
      </button>
    ))}
  </div>
)

const SecretPill = ({ label, ready }: { label: string; ready: boolean }) => (
  <span className={`rounded border px-2 py-1 text-xs ${ready ? 'border-emerald-500/40 text-emerald-200' : 'border-gray-700 text-gray-400'}`}>
    {label}: {ready ? 'present' : 'missing'}
  </span>
)

const CredentialStatus = ({
  label,
  ready,
  readyText = 'saved',
  missingText = 'missing',
}: {
  label: string
  ready: boolean
  readyText?: string
  missingText?: string
}) => (
  <div className="flex min-h-10 items-center justify-between gap-3 rounded border border-gray-800 bg-gray-950 px-3 py-2 text-sm">
    <span className="text-gray-200">{label}</span>
    <span className={`inline-flex items-center gap-2 ${ready ? 'text-emerald-300' : 'text-gray-500'}`}>
      {ready && <CheckIcon className="h-4 w-4 fill-current" />}
      {ready ? readyText : missingText}
    </span>
  </div>
)

export default function OperatorEmailPage() {
  const {
    config,
    history,
    loading,
    saving,
    error,
    fetchConfig,
    updateConfig,
    setProviderSecret,
    sendTest,
    fetchHistory,
  } = useOperatorEmailStore()
  const [emailForm, setEmailForm] = useState<EmailConfig | null>(null)
  const [operatorForm, setOperatorForm] = useState<OperatorEmailsConfig | null>(null)
  const [sender, setSender] = useState<SenderFields>({ displayName: DEFAULT_DISPLAY_NAME, senderId: '', senderDomain: '', replyTo: '', baseUrl: '' })
  const [recipientRows, setRecipientRows] = useState<OperatorRecipientRow[]>([])
  const [newRecipient, setNewRecipient] = useState<OperatorRecipientRow>({ email: '', alerts: true, weekly: true, security: true })
  const [resendSecret, setResendSecret] = useState('')
  const [sesEditing, setSesEditing] = useState(false)
  const [sesAccessKey, setSesAccessKey] = useState('')
  const [sesSecretKey, setSesSecretKey] = useState('')
  const [testTo, setTestTo] = useState('')
  const [dryRun, setDryRun] = useState(false)
  const [testResult, setTestResult] = useState<string | null>(null)
  const [localError, setLocalError] = useState<string | null>(null)

  useEffect(() => {
    fetchConfig().catch(() => undefined)
    fetchHistory(25).catch(() => undefined)
  }, [fetchConfig, fetchHistory])

  useEffect(() => {
    if (!config) return
    const rows = rowsFromRecipients(config.operator_emails.recipients)
    setEmailForm(cloneEmail(config.email))
    setOperatorForm(cloneOperator(config.operator_emails))
    setSender(parseSender(config.email))
    setRecipientRows(rows)
    setTestTo(current => current || preferredRecipient(rows))
    if (config.email.provider !== 'ses') {
      setSesEditing(false)
      setSesAccessKey('')
      setSesSecretKey('')
    }
  }, [config])

  if (loading && !config) return <CircleNotchLoader />
  if (!config || !emailForm || !operatorForm) return null

  const activeProvider = emailForm.provider !== 'none'
  const secrets = config.secrets
  const hasSesCredentials = secrets.ses_access_key_id || secrets.ses_secret_access_key
  const canSaveProvider = !activeProvider || Boolean(buildSenderAddress(sender))
  const canAddRecipient = newRecipient.email.trim() && recipientGroups.some(group => newRecipient[group])

  const changeProvider = (provider: EmailProvider) => {
    setEmailForm({ ...emailForm, provider, enabled: provider === 'none' ? false : true })
    setOperatorForm({ ...operatorForm, enabled: provider === 'none' ? false : true })
    if (provider !== 'ses') {
      setSesEditing(false)
      setSesAccessKey('')
      setSesSecretKey('')
    }
  }

  const updateSesSecrets = () => {
    setLocalError(null)
    setSesSecretKey('')
    setSesAccessKey('')
    setSesEditing(true)
  }

  const saveProviderSetup = async () => {
    setLocalError(null)

    if (emailForm.provider === 'none') {
      await updateConfig({
        email: { provider: 'none', enabled: false },
        operator_emails: { enabled: false },
      })
      return
    }

    const from = buildSenderAddress(sender)
    if (!from) {
      setLocalError('Sender ID requires a sender domain or a full email address.')
      return
    }

    const emailPatch: NonNullable<OperatorEmailConfigPatch['email']> = {
      provider: emailForm.provider,
      enabled: emailForm.enabled,
      from,
      reply_to: sender.replyTo.trim() || null,
      base_url: sender.baseUrl.trim() || null,
    }

    if (emailForm.provider === 'ses') emailPatch.ses = { region: emailForm.ses.region }

    await updateConfig({
      email: emailPatch,
      operator_emails: { enabled: operatorForm.enabled },
    })

    if (emailForm.provider === 'resend' && resendSecret.trim()) {
      await setProviderSecret({ provider: 'resend', api_key: resendSecret.trim() })
      setResendSecret('')
    }

    if (emailForm.provider === 'ses' && (sesAccessKey.trim() || sesSecretKey.trim())) {
      await setProviderSecret({
        provider: 'ses',
        access_key_id: sesAccessKey.trim() || undefined,
        secret_access_key: sesSecretKey.trim() || undefined,
      })
      setSesAccessKey('')
      setSesSecretKey('')
      setSesEditing(false)
    } else if (emailForm.provider === 'ses' && hasSesCredentials) {
      setSesEditing(false)
    }
  }

  const addRecipient = () => {
    const email = newRecipient.email.trim()
    if (!email || !canAddRecipient) return

    setRecipientRows(rows => {
      const existing = rows.find(row => row.email === email)
      if (existing) {
        return rows.map(row =>
          row.email === email
            ? {
                ...row,
                alerts: row.alerts || newRecipient.alerts,
                weekly: row.weekly || newRecipient.weekly,
                security: row.security || newRecipient.security,
              }
            : row,
        )
      }
      return [...rows, { ...newRecipient, email }].sort((a, b) => a.email.localeCompare(b.email))
    })

    setTestTo(current => current || email)
    setNewRecipient({ email: '', alerts: true, weekly: true, security: true })
  }

  const updateRecipientRow = (email: string, group: RecipientGroup, value: boolean) => {
    setRecipientRows(rows => rows.map(row => (row.email === email ? { ...row, [group]: value } : row)))
  }

  const removeRecipient = (email: string) => {
    setRecipientRows(rows => rows.filter(row => row.email !== email))
  }

  const saveRecipients = async () => {
    const recipients = recipientsFromRows(recipientRows)
    setOperatorForm({ ...operatorForm, recipients })
    await updateConfig({ operator_emails: { recipients } })
  }

  const saveNotificationSettings = async () => {
    await updateConfig({
      operator_emails: {
        ...operatorForm,
        recipients: recipientsFromRows(recipientRows),
      },
    })
  }

  const runTest = async () => {
    const result = await sendTest({ to: testTo, dry_run: dryRun })
    setTestResult(result.status === 'sent' ? `Sent ${result.subject}` : `Rendered ${result.subject}`)
  }

  return (
    <AdminPage title="Operator Email" description="Provider setup, recipients, recaps, alerts, and delivery checks.">
      <div className="mx-auto flex w-full max-w-7xl flex-col gap-4 p-4">
        {(error || localError) && <div className="rounded border border-red-500/40 bg-red-950/40 p-3 text-sm text-red-100">{localError || error}</div>}

        <Section title="Provider Setup" icon={EnvelopeIcon}>
          <ProviderSegment value={emailForm.provider} onChange={changeProvider} />

          {!activeProvider ? (
            <div className="rounded border border-gray-800 bg-gray-950 p-4 text-sm text-gray-300">
              Email delivery is disabled for operator notifications.
            </div>
          ) : (
            <>
              <div className="grid gap-3 md:grid-cols-2">
                <ToggleField label="Email delivery" checked={emailForm.enabled} onChange={enabled => setEmailForm({ ...emailForm, enabled })} />
                <ToggleField
                  label="Operator notifications"
                  checked={operatorForm.enabled}
                  onChange={enabled => setOperatorForm({ ...operatorForm, enabled })}
                />
              </div>

              <div className="grid gap-3 rounded border border-gray-800 bg-gray-950 p-3">
                {emailForm.provider === 'resend' && (
                  <div className="flex flex-wrap items-center gap-2 text-sm">
                    <span className={secrets.available ? 'text-emerald-300' : 'text-red-300'}>
                      Secrets manager: {secrets.available ? 'available' : 'unavailable'}
                    </span>
                    <SecretPill label="Resend API key" ready={secrets.resend_api_key} />
                  </div>
                )}

                {emailForm.provider === 'resend' && (
                  <TextField label="Resend API key" type="password" value={resendSecret} onChange={setResendSecret} />
                )}

                {emailForm.provider === 'ses' && (
                  <div className="grid gap-3">
                    <TextField
                      label="SES region"
                      value={emailForm.ses.region}
                      onChange={region => setEmailForm({ ...emailForm, ses: { ...emailForm.ses, region } })}
                    />

                    <div className="grid gap-2">
                      <CredentialStatus label="Secrets manager" ready={secrets.available} readyText="available" missingText="unavailable" />
                    </div>

                    {!sesEditing && hasSesCredentials && (
                      <div className="flex flex-wrap items-center justify-between gap-3 rounded border border-emerald-500/30 bg-emerald-950/20 px-3 py-2">
                        <span className="inline-flex items-center gap-2 text-sm font-medium text-emerald-200">
                          <CheckIcon className="h-5 w-5 fill-current text-emerald-300" />
                          Added
                        </span>
                        <ActionButton icon={EditIcon} disabled={saving || !secrets.available} onClick={updateSesSecrets}>
                          Update
                        </ActionButton>
                      </div>
                    )}

                    {(sesEditing || !hasSesCredentials) && (
                      <div className="grid gap-3 rounded border border-gray-800 bg-gray-950 p-3">
                        <TextField label="SES access key ID" value={sesAccessKey} onChange={setSesAccessKey} />
                        <TextField label="SES secret access key" type="password" value={sesSecretKey} onChange={setSesSecretKey} />
                      </div>
                    )}
                  </div>
                )}
              </div>

              <div className="grid gap-3 md:grid-cols-3">
                <TextField
                  label="Display name"
                  value={sender.displayName}
                  onChange={displayName => setSender({ ...sender, displayName })}
                />
                <TextField
                  label="Sender ID"
                  placeholder="vaulthalla"
                  value={sender.senderId}
                  onChange={senderId => setSender({ ...sender, senderId })}
                />
                <TextField
                  label="Sender domain"
                  placeholder="example.com"
                  value={sender.senderDomain}
                  onChange={senderDomain => setSender({ ...sender, senderDomain })}
                />
                <TextField label="Reply-To" value={sender.replyTo} onChange={replyTo => setSender({ ...sender, replyTo })} />
              </div>

              <details className="rounded border border-gray-800 bg-gray-950 p-3">
                <summary className="cursor-pointer text-sm text-gray-300">Template links</summary>
                <div className="mt-3">
                  <TextField label="Link base URL" value={sender.baseUrl} onChange={baseUrl => setSender({ ...sender, baseUrl })} />
                </div>
              </details>
            </>
          )}

          <ActionButton icon={SaveIcon} disabled={saving || !canSaveProvider} onClick={saveProviderSetup}>
            Save provider setup
          </ActionButton>
        </Section>

        {activeProvider && (
          <div className="grid gap-4 xl:grid-cols-[minmax(0,1.2fr)_minmax(360px,0.8fr)]">
            <div className="space-y-4">
              <Section title="Operator Recipients" icon={BellIcon}>
                <div className="grid gap-3 rounded border border-gray-800 bg-gray-950 p-3 md:grid-cols-[minmax(0,1fr)_auto] md:items-end">
                  <TextField label="Email" placeholder="ops@example.com" value={newRecipient.email} onChange={email => setNewRecipient({ ...newRecipient, email })} />
                  <div className="flex flex-wrap gap-3">
                    {recipientGroups.map(group => (
                      <label key={group} className="flex min-h-10 items-center gap-2 rounded border border-gray-800 px-3 text-sm capitalize text-gray-200">
                        <input
                          className="h-4 w-4 accent-cyan-400"
                          type="checkbox"
                          checked={newRecipient[group]}
                          onChange={event => setNewRecipient({ ...newRecipient, [group]: event.target.checked })}
                        />
                        {group}
                      </label>
                    ))}
                    <ActionButton icon={PlusIcon} disabled={saving || !canAddRecipient} onClick={addRecipient}>
                      Add
                    </ActionButton>
                  </div>
                </div>

                <div className="divide-y divide-gray-800 rounded border border-gray-800">
                  {recipientRows.length === 0 ? (
                    <div className="p-3 text-sm text-gray-400">No operator recipients.</div>
                  ) : (
                    recipientRows.map(row => (
                      <div key={row.email} className="grid gap-3 p-3 text-sm md:grid-cols-[minmax(0,1fr)_auto] md:items-center">
                        <span className="min-w-0 break-all text-gray-100">{row.email}</span>
                        <div className="flex flex-wrap items-center gap-2 md:justify-end">
                          {recipientGroups.map(group => (
                            <label key={group} className="flex items-center gap-2 rounded border border-gray-800 px-2 py-1 capitalize text-gray-200">
                              <input
                                className="h-4 w-4 accent-cyan-400"
                                type="checkbox"
                                checked={row[group]}
                                onChange={event => updateRecipientRow(row.email, group, event.target.checked)}
                              />
                              {group}
                            </label>
                          ))}
                          <button
                            className="rounded p-2 text-red-300 hover:bg-red-950"
                            title="Remove recipient"
                            type="button"
                            onClick={() => removeRecipient(row.email)}>
                            <TrashIcon className="h-4 w-4 fill-current" />
                          </button>
                        </div>
                      </div>
                    ))
                  )}
                </div>

                <ActionButton icon={SaveIcon} disabled={saving} onClick={saveRecipients}>
                  Save recipients
                </ActionButton>
              </Section>

              <div className="grid gap-4 lg:grid-cols-2">
                <Section title="Weekly Recap" icon={CalendarIcon}>
                  <ToggleField
                    label="Enabled"
                    checked={operatorForm.weekly_digest.enabled}
                    onChange={enabled => setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, enabled } })}
                  />
                  <label className="flex flex-col gap-1 text-sm text-gray-300">
                    Weekday
                    <select
                      className="min-h-10 rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
                      value={operatorForm.weekly_digest.weekday}
                      onChange={event =>
                        setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, weekday: event.target.value } })
                      }>
                      {weekdays.map(day => (
                        <option key={day} value={day}>
                          {day}
                        </option>
                      ))}
                    </select>
                  </label>
                  <div className="grid gap-3 sm:grid-cols-2">
                    <NumberField
                      label="Hour"
                      min={0}
                      max={23}
                      value={operatorForm.weekly_digest.hour_local}
                      onChange={hour_local => setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, hour_local } })}
                    />
                    <TextField
                      label="Timezone"
                      value={operatorForm.weekly_digest.timezone}
                      onChange={timezone => setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, timezone } })}
                    />
                  </div>
                </Section>

                <Section title="Security Alerts" icon={ShieldIcon}>
                  <ToggleField
                    label="Enabled"
                    checked={operatorForm.security_alerts.enabled}
                    onChange={enabled => setOperatorForm({ ...operatorForm, security_alerts: { ...operatorForm.security_alerts, enabled } })}
                  />
                  <ToggleField
                    label="Admin role changes"
                    checked={operatorForm.security_alerts.admin_role_changes}
                    onChange={admin_role_changes =>
                      setOperatorForm({ ...operatorForm, security_alerts: { ...operatorForm.security_alerts, admin_role_changes } })
                    }
                  />
                </Section>
              </div>

              <Section title="Alert Policy" icon={BellIcon}>
                <div className="grid gap-3 md:grid-cols-3">
                  <ToggleField
                    label="Enabled"
                    checked={operatorForm.alerting.enabled}
                    onChange={enabled => setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, enabled } })}
                  />
                  <ToggleField
                    label="Send recovery"
                    checked={operatorForm.alerting.send_recovery}
                    onChange={send_recovery => setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, send_recovery } })}
                  />
                  <label className="flex flex-col gap-1 text-sm text-gray-300">
                    Minimum severity
                    <select
                      className="min-h-10 rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
                      value={operatorForm.alerting.min_severity}
                      onChange={event =>
                        setOperatorForm({
                          ...operatorForm,
                          alerting: { ...operatorForm.alerting, min_severity: event.target.value as OperatorEmailsConfig['alerting']['min_severity'] },
                        })
                      }>
                      <option value="info">info</option>
                      <option value="warning">warning</option>
                      <option value="critical">critical</option>
                    </select>
                  </label>
                  <NumberField
                    label="Dedupe minutes"
                    min={1}
                    value={operatorForm.alerting.dedupe_window_minutes}
                    onChange={dedupe_window_minutes => setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, dedupe_window_minutes } })}
                  />
                  <NumberField
                    label="Repeat hours"
                    min={1}
                    value={operatorForm.alerting.repeat_after_hours}
                    onChange={repeat_after_hours => setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, repeat_after_hours } })}
                  />
                  <NumberField
                    label="Health poll seconds"
                    min={15}
                    value={operatorForm.alerting.health_poll_seconds}
                    onChange={health_poll_seconds => setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, health_poll_seconds } })}
                  />
                </div>
                <ActionButton icon={SaveIcon} disabled={saving} onClick={saveNotificationSettings}>
                  Save notification settings
                </ActionButton>
              </Section>
            </div>

            <div className="space-y-4">
              <Section title="Test Send" icon={SendIcon}>
                <TextField label="Recipient" value={testTo} onChange={setTestTo} />
                <ToggleField label="Dry run" checked={dryRun} onChange={setDryRun} />
                <ActionButton icon={SendIcon} disabled={saving || !testTo.trim()} onClick={runTest}>
                  {dryRun ? 'Render test' : 'Send test'}
                </ActionButton>
                {testResult && <div className="rounded border border-emerald-500/30 bg-emerald-950/30 p-3 text-sm text-emerald-100">{testResult}</div>}
              </Section>

              <Section title="History" icon={RefreshIcon}>
                <ActionButton icon={RefreshIcon} disabled={saving} onClick={() => fetchHistory(25).catch(() => undefined)}>
                  Refresh
                </ActionButton>
                <div className="divide-y divide-gray-800 rounded border border-gray-800">
                  {history.length === 0 ? (
                    <div className="p-3 text-sm text-gray-400">No delivery records.</div>
                  ) : (
                    history.slice(0, 12).map(record => (
                      <div key={record.id} className="space-y-1 p-3 text-sm">
                        <div className="flex items-center justify-between gap-3">
                          <span className="font-medium text-gray-100">{record.status}</span>
                          <span className="text-xs text-gray-500">{record.created_at}</span>
                        </div>
                        <div className="break-words text-gray-300">{record.subject}</div>
                        <div className="text-xs text-gray-500">
                          {record.provider} / {record.event_type} / {record.recipient_count} recipients
                        </div>
                        {record.error_summary && <div className="break-words text-xs text-red-300">{record.error_summary}</div>}
                      </div>
                    ))
                  )}
                </div>
              </Section>
            </div>
          </div>
        )}
      </div>
    </AdminPage>
  )
}
