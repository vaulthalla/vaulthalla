'use client'

import React, { useEffect, useState } from 'react'
import EnvelopeIcon from '@/fa-duotone/envelope.svg'
import KeyIcon from '@/fa-duotone/key.svg'
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
import { EmailConfig, EmailProvider, OperatorEmailsConfig, RecipientGroup } from '@/models/operatorEmail'
import { useOperatorEmailStore } from '@/stores/operatorEmailStore'

const weekdays = ['sunday', 'monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday']
const recipientGroups: RecipientGroup[] = ['alerts', 'weekly', 'security']

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
}: {
  label: string
  value: string
  onChange: (value: string) => void
  type?: string
}) => (
  <label className="flex flex-col gap-1 text-sm text-gray-300">
    {label}
    <input
      className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white outline-none focus:border-cyan-400"
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
      className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white outline-none focus:border-cyan-400"
      type="number"
      min={min}
      max={max}
      value={value}
      onChange={event => onChange(Number(event.target.value))}
    />
  </label>
)

const ToggleField = ({ label, checked, onChange }: { label: string; checked: boolean; onChange: (value: boolean) => void }) => (
  <label className="flex items-center justify-between gap-3 rounded border border-gray-800 bg-gray-950 px-3 py-2 text-sm text-gray-200">
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

export default function OperatorEmailPage() {
  const { config, history, loading, saving, error, fetchConfig, updateConfig, setProviderSecret, sendTest, fetchHistory } = useOperatorEmailStore()
  const [emailForm, setEmailForm] = useState<EmailConfig | null>(null)
  const [operatorForm, setOperatorForm] = useState<OperatorEmailsConfig | null>(null)
  const [recipientGroup, setRecipientGroup] = useState<RecipientGroup>('alerts')
  const [recipientInput, setRecipientInput] = useState('')
  const [resendSecret, setResendSecret] = useState('')
  const [sesAccessKey, setSesAccessKey] = useState('')
  const [sesSecretKey, setSesSecretKey] = useState('')
  const [testTo, setTestTo] = useState('')
  const [dryRun, setDryRun] = useState(false)
  const [testResult, setTestResult] = useState<string | null>(null)

  useEffect(() => {
    fetchConfig().catch(() => undefined)
    fetchHistory(25).catch(() => undefined)
  }, [fetchConfig, fetchHistory])

  useEffect(() => {
    if (!config) return
    setEmailForm(cloneEmail(config.email))
    setOperatorForm(cloneOperator(config.operator_emails))
  }, [config])

  if (loading && !config) return <CircleNotchLoader />
  if (!config || !emailForm || !operatorForm) return null

  const saveEmail = async () => {
    await updateConfig({
      email: {
        ...emailForm,
        reply_to: emailForm.reply_to?.trim() || null,
        base_url: emailForm.base_url?.trim() || null,
        ses: {
          ...emailForm.ses,
          endpoint: emailForm.ses.endpoint?.trim() || null,
        },
      },
    })
  }

  const saveOperator = async () => {
    await updateConfig({ operator_emails: operatorForm })
  }

  const addRecipient = async () => {
    const recipient = recipientInput.trim()
    if (!recipient || operatorForm.recipients[recipientGroup].includes(recipient)) return
    const next = cloneOperator(operatorForm)
    next.recipients[recipientGroup] = [...next.recipients[recipientGroup], recipient]
    setOperatorForm(next)
    setRecipientInput('')
    await updateConfig({ operator_emails: { recipients: next.recipients } })
  }

  const removeRecipient = async (recipient: string) => {
    const next = cloneOperator(operatorForm)
    next.recipients[recipientGroup] = next.recipients[recipientGroup].filter(value => value !== recipient)
    setOperatorForm(next)
    await updateConfig({ operator_emails: { recipients: next.recipients } })
  }

  const storeResendSecret = async () => {
    await setProviderSecret({ provider: 'resend', api_key: resendSecret })
    setResendSecret('')
  }

  const storeSesSecret = async () => {
    await setProviderSecret({ provider: 'ses', access_key_id: sesAccessKey, secret_access_key: sesSecretKey })
    setSesAccessKey('')
    setSesSecretKey('')
  }

  const runTest = async () => {
    const result = await sendTest({ to: testTo, dry_run: dryRun })
    setTestResult(result.status === 'sent' ? `Sent ${result.subject}` : `Rendered ${result.subject}`)
  }

  const secrets = config.secrets
  const selectedRecipients = operatorForm.recipients[recipientGroup]

  return (
    <AdminPage title="Operator Email" description="Provider setup, recipients, recaps, alerts, and delivery checks.">
      <div className="mx-auto grid w-full max-w-7xl gap-4 p-4 xl:grid-cols-[minmax(0,1.2fr)_minmax(360px,0.8fr)]">
        <div className="space-y-4">
          {error && <div className="rounded border border-red-500/40 bg-red-950/40 p-3 text-sm text-red-100">{error}</div>}

          <Section title="Delivery" icon={EnvelopeIcon}>
            <ProviderSegment value={emailForm.provider} onChange={provider => setEmailForm({ ...emailForm, provider })} />
            <div className="grid gap-3 md:grid-cols-2">
              <ToggleField label="Email enabled" checked={emailForm.enabled} onChange={enabled => setEmailForm({ ...emailForm, enabled })} />
              <ToggleField
                label="Operator emails enabled"
                checked={operatorForm.enabled}
                onChange={enabled => setOperatorForm({ ...operatorForm, enabled })}
              />
            </div>
            <div className="grid gap-3 md:grid-cols-2">
              <TextField label="From" value={emailForm.from} onChange={from => setEmailForm({ ...emailForm, from })} />
              <TextField label="Reply-To" value={emailForm.reply_to || ''} onChange={reply_to => setEmailForm({ ...emailForm, reply_to })} />
              <TextField label="Base URL" value={emailForm.base_url || ''} onChange={base_url => setEmailForm({ ...emailForm, base_url })} />
              <TextField
                label="Resend endpoint"
                value={emailForm.resend.endpoint}
                onChange={endpoint => setEmailForm({ ...emailForm, resend: { ...emailForm.resend, endpoint } })}
              />
              <TextField
                label="SES region"
                value={emailForm.ses.region}
                onChange={region => setEmailForm({ ...emailForm, ses: { ...emailForm.ses, region } })}
              />
              <TextField
                label="SES endpoint"
                value={emailForm.ses.endpoint || ''}
                onChange={endpoint => setEmailForm({ ...emailForm, ses: { ...emailForm.ses, endpoint } })}
              />
            </div>
            <div className="flex flex-wrap gap-2">
              <ActionButton icon={SaveIcon} disabled={saving} onClick={saveEmail}>
                Save delivery
              </ActionButton>
              <ActionButton icon={SaveIcon} disabled={saving} onClick={saveOperator}>
                Save operator state
              </ActionButton>
            </div>
          </Section>

          <Section title="Recipients" icon={BellIcon}>
            <div className="grid grid-cols-3 rounded border border-gray-700 bg-gray-950 p-1 text-sm">
              {recipientGroups.map(group => (
                <button
                  key={group}
                  className={`rounded px-2 py-2 capitalize ${recipientGroup === group ? 'bg-cyan-400 text-gray-950' : 'text-gray-300 hover:bg-white/10'}`}
                  onClick={() => setRecipientGroup(group)}
                  type="button">
                  {group}
                </button>
              ))}
            </div>
            <div className="flex flex-col gap-2 sm:flex-row">
              <input
                className="min-h-10 flex-1 rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white outline-none focus:border-cyan-400"
                placeholder="ops@example.com"
                value={recipientInput}
                onChange={event => setRecipientInput(event.target.value)}
              />
              <ActionButton icon={PlusIcon} disabled={saving || !recipientInput.trim()} onClick={addRecipient}>
                Add
              </ActionButton>
            </div>
            <div className="divide-y divide-gray-800 rounded border border-gray-800">
              {selectedRecipients.length === 0 ? (
                <div className="p-3 text-sm text-gray-400">No recipients.</div>
              ) : (
                selectedRecipients.map(recipient => (
                  <div key={recipient} className="flex items-center justify-between gap-3 px-3 py-2 text-sm">
                    <span className="min-w-0 break-all text-gray-100">{recipient}</span>
                    <button
                      className="rounded p-2 text-red-300 hover:bg-red-950"
                      title="Remove recipient"
                      type="button"
                      onClick={() => removeRecipient(recipient)}>
                      <TrashIcon className="h-4 w-4 fill-current" />
                    </button>
                  </div>
                ))
              )}
            </div>
          </Section>

          <div className="grid gap-4 lg:grid-cols-2">
            <Section title="Weekly Recap" icon={CalendarIcon}>
              <ToggleField
                label="Enabled"
                checked={operatorForm.weekly_digest.enabled}
                onChange={enabled =>
                  setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, enabled } })
                }
              />
              <label className="flex flex-col gap-1 text-sm text-gray-300">
                Weekday
                <select
                  className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
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
                  onChange={hour_local =>
                    setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, hour_local } })
                  }
                />
                <TextField
                  label="Timezone"
                  value={operatorForm.weekly_digest.timezone}
                  onChange={timezone =>
                    setOperatorForm({ ...operatorForm, weekly_digest: { ...operatorForm.weekly_digest, timezone } })
                  }
                />
              </div>
              <ActionButton icon={SaveIcon} disabled={saving} onClick={saveOperator}>
                Save recap
              </ActionButton>
            </Section>

            <Section title="Security Alerts" icon={ShieldIcon}>
              <ToggleField
                label="Enabled"
                checked={operatorForm.security_alerts.enabled}
                onChange={enabled =>
                  setOperatorForm({ ...operatorForm, security_alerts: { ...operatorForm.security_alerts, enabled } })
                }
              />
              <ToggleField
                label="Admin role changes"
                checked={operatorForm.security_alerts.admin_role_changes}
                onChange={admin_role_changes =>
                  setOperatorForm({ ...operatorForm, security_alerts: { ...operatorForm.security_alerts, admin_role_changes } })
                }
              />
              <ActionButton icon={SaveIcon} disabled={saving} onClick={saveOperator}>
                Save security
              </ActionButton>
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
                  className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
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
                onChange={dedupe_window_minutes =>
                  setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, dedupe_window_minutes } })
                }
              />
              <NumberField
                label="Repeat hours"
                min={1}
                value={operatorForm.alerting.repeat_after_hours}
                onChange={repeat_after_hours =>
                  setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, repeat_after_hours } })
                }
              />
              <NumberField
                label="Health poll seconds"
                min={15}
                value={operatorForm.alerting.health_poll_seconds}
                onChange={health_poll_seconds =>
                  setOperatorForm({ ...operatorForm, alerting: { ...operatorForm.alerting, health_poll_seconds } })
                }
              />
            </div>
            <ActionButton icon={SaveIcon} disabled={saving} onClick={saveOperator}>
              Save alert policy
            </ActionButton>
          </Section>
        </div>

        <div className="space-y-4">
          <Section title="Secrets" icon={KeyIcon}>
            <div className="grid gap-2 text-sm">
              <div className="flex items-center justify-between rounded border border-gray-800 bg-gray-950 px-3 py-2">
                <span>Secrets manager</span>
                <span className={secrets.available ? 'text-emerald-300' : 'text-red-300'}>{secrets.available ? 'available' : 'unavailable'}</span>
              </div>
              <div className="grid gap-2 sm:grid-cols-3">
                <span className={`rounded border px-2 py-1 ${secrets.resend_api_key ? 'border-emerald-500/40 text-emerald-200' : 'border-gray-700 text-gray-400'}`}>
                  Resend key
                </span>
                <span className={`rounded border px-2 py-1 ${secrets.ses_access_key_id ? 'border-emerald-500/40 text-emerald-200' : 'border-gray-700 text-gray-400'}`}>
                  SES access
                </span>
                <span className={`rounded border px-2 py-1 ${secrets.ses_secret_access_key ? 'border-emerald-500/40 text-emerald-200' : 'border-gray-700 text-gray-400'}`}>
                  SES secret
                </span>
              </div>
            </div>
            <TextField label="Resend API key" type="password" value={resendSecret} onChange={setResendSecret} />
            <ActionButton icon={KeyIcon} disabled={saving || !resendSecret} onClick={storeResendSecret}>
              Store Resend key
            </ActionButton>
            <div className="grid gap-3">
              <TextField label="SES access key ID" type="password" value={sesAccessKey} onChange={setSesAccessKey} />
              <TextField label="SES secret access key" type="password" value={sesSecretKey} onChange={setSesSecretKey} />
            </div>
            <ActionButton icon={KeyIcon} disabled={saving || (!sesAccessKey && !sesSecretKey)} onClick={storeSesSecret}>
              Store SES secrets
            </ActionButton>
          </Section>

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
                      {record.provider} · {record.event_type} · {record.recipient_count} recipients
                    </div>
                    {record.error_summary && <div className="break-words text-xs text-red-300">{record.error_summary}</div>}
                  </div>
                ))
              )}
            </div>
          </Section>
        </div>
      </div>
    </AdminPage>
  )
}
