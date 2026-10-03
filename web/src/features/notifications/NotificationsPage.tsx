'use client'

import React, { useState } from 'react'
import { api } from '@/lib/session'
import { cn } from '@/util/cn'
import { useWs, invalidate } from '@/lib/query'
import { PageHeader, Panel } from '@/components/ui/Panel'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { Field, Input, Select, Label } from '@/components/ui/Field'
import { Checkbox, Switch, SwitchRow } from '@/components/ui/Choice'
import { Segmented } from '@/components/ui/Tabs'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { EmptyState, InlineError, QueryState } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { FlaskIcon, FloppyDiskIcon, PaperPlaneIcon, PlusIcon, TrashIcon } from '@/components/ui/icons'
import { DASH, formatDateTime, formatRelative, titleCase } from '@/lib/format'
import { UnitInput, EMAIL_RE } from '@/features/settings/controls'
import { EmptyRows, Section } from '@/features/cost/bits'
import { useIsSuperAdmin } from '@/features/cost/queries'
import {
  GROUPS,
  SEVERITIES,
  WEEKDAYS,
  joinFrom,
  splitFrom,
  toHistory,
  toSettings,
  type EmailSettings,
  type Group,
  type HistoryRecord,
  type Severity,
  type Provider,
} from '@/features/notifications/model'

type Patch = Parameters<typeof api.send<'email.config.update'>>[1]

const saveConfig = async (patch: Patch, success: string) => {
  await api.send('email.config.update', patch)
  await invalidate('email.config.get', 'settings.get')
  notify.success(success)
}

const useSaver = () => {
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)
  const run = async (work: () => Promise<unknown>) => {
    setBusy(true)
    setError(null)
    try {
      await work()
      return true
    } catch (err) {
      setError(err)
      return false
    } finally {
      setBusy(false)
    }
  }
  return { busy, error, run }
}

const Stored = ({ value, label }: { value: boolean | null; label: string }) => (
  <Badge tone={value ? 'accent' : 'neutral'} className={value ? undefined : 'text-fg-subtle'}>
    {label}:{' '}
    {value === null ?
      'unknown'
    : value ?
      'stored'
    : 'not set'}
  </Badge>
)

// ---- provider ---------------------------------------------------------------------------------------------------

const ProviderPanel = ({ settings }: { settings: EmailSettings }) => {
  const { email, secrets } = settings
  const initialFrom = splitFrom(email.from)
  const [provider, setProvider] = useState<Provider>(email.provider)
  const [enabled, setEnabled] = useState(email.enabled)
  const [operatorEnabled, setOperatorEnabled] = useState(settings.operator.enabled)
  const [name, setName] = useState(initialFrom.name)
  const [address, setAddress] = useState(initialFrom.address)
  const [replyTo, setReplyTo] = useState(email.reply_to ?? '')
  const [baseUrl, setBaseUrl] = useState(email.base_url ?? '')
  const [region, setRegion] = useState(email.ses.region)
  const [sesEndpoint, setSesEndpoint] = useState(email.ses.endpoint ?? '')
  const [resendKey, setResendKey] = useState('')
  const [sesAccess, setSesAccess] = useState('')
  const [sesSecret, setSesSecret] = useState('')
  const { busy, error, run } = useSaver()

  const problems = {
    address: provider !== 'none' && !EMAIL_RE.test(address.trim()) ? 'Use a full address, e.g. ops@example.com' : null,
    replyTo: replyTo.trim() && !EMAIL_RE.test(replyTo.trim()) ? 'Use a full address' : null,
    baseUrl:
      baseUrl.trim() && !/^https?:\/\/\S+$/.test(baseUrl.trim()) ?
        'Use a full URL, e.g. https://vault.example.com'
      : null,
  }
  const invalid = Object.values(problems).some(Boolean)
  const dirty =
    provider !== email.provider
    || enabled !== email.enabled
    || operatorEnabled !== settings.operator.enabled
    || joinFrom(name, address) !== email.from
    || (replyTo.trim() || null) !== email.reply_to
    || (baseUrl.trim() || null) !== email.base_url
    || region.trim() !== email.ses.region
    || (sesEndpoint.trim() || null) !== email.ses.endpoint
    || Boolean(resendKey.trim() || sesAccess.trim() || sesSecret.trim())

  const save = () =>
    run(async () => {
      if (provider === 'none') {
        await saveConfig(
          { email: { provider: 'none', enabled: false }, operator_emails: { enabled: false } },
          'Email delivery turned off',
        )
        return
      }
      const patch: NonNullable<Patch['email']> = {}
      if (provider !== email.provider) patch.provider = provider
      if (enabled !== email.enabled) patch.enabled = enabled
      const from = joinFrom(name, address)
      if (from !== email.from) patch.from = from
      if ((replyTo.trim() || null) !== email.reply_to) patch.reply_to = replyTo.trim() || null
      if ((baseUrl.trim() || null) !== email.base_url) patch.base_url = baseUrl.trim() || null
      if (
        provider === 'ses'
        && (region.trim() !== email.ses.region || (sesEndpoint.trim() || null) !== email.ses.endpoint)
      )
        patch.ses = { region: region.trim(), endpoint: sesEndpoint.trim() || null }
      const operatorChanged = operatorEnabled !== settings.operator.enabled
      if (Object.keys(patch).length || operatorChanged)
        await api.send('email.config.update', {
          ...(Object.keys(patch).length ? { email: patch } : {}),
          ...(operatorChanged ? { operator_emails: { enabled: operatorEnabled } } : {}),
        })
      // Secrets are write-only: the server only ever says whether one is stored.
      if (provider === 'resend' && resendKey.trim())
        await api.send('email.provider.secret.set', { provider: 'resend', api_key: resendKey.trim() })
      if (provider === 'ses' && (sesAccess.trim() || sesSecret.trim()))
        await api.send('email.provider.secret.set', {
          provider: 'ses',
          ...(sesAccess.trim() ? { access_key_id: sesAccess.trim() } : {}),
          ...(sesSecret.trim() ? { secret_access_key: sesSecret.trim() } : {}),
        })
      setResendKey('')
      setSesAccess('')
      setSesSecret('')
      await invalidate('email.config.get', 'settings.get')
      notify.success('Email delivery saved')
    })

  return (
    <Panel
      id="provider"
      title="Delivery"
      description="How operator email leaves this server."
      actions={
        <Button variant="primary" onClick={() => void save()} loading={busy} disabled={invalid || !dirty}>
          <FloppyDiskIcon aria-hidden />
          Save delivery
        </Button>
      }>
      <div className="space-y-5">
        <div className="flex flex-wrap items-center gap-3">
          <Segmented
            label="Email provider"
            value={provider}
            onChange={setProvider}
            options={[
              { value: 'none', label: 'Off' },
              { value: 'resend', label: 'Resend' },
              { value: 'ses', label: 'Amazon SES' },
            ]}
          />
          {secrets.available === false ?
            <Badge tone="neutral">Secrets manager unavailable</Badge>
          : null}
        </div>

        {provider === 'none' ?
          <p className="text-fg-subtle text-sm">No operator email is sent. Pick a provider to set up delivery.</p>
        : <>
            <div className="grid gap-3 sm:grid-cols-2">
              <SwitchRow
                id="email-enabled"
                label="Send email"
                hint="The master switch for every operator email."
                checked={enabled}
                onCheckedChange={setEnabled}
              />
              <SwitchRow
                id="operator-enabled"
                label="Operator notifications"
                hint="Alerts, recaps and security notices to the recipients below."
                checked={operatorEnabled}
                onCheckedChange={setOperatorEnabled}
              />
            </div>
            <div className="grid gap-4 sm:grid-cols-2">
              <Field label="From name" htmlFor="from-name">
                <Input id="from-name" value={name} onChange={e => setName(e.target.value)} placeholder="Vaulthalla" />
              </Field>
              <Field label="From address" htmlFor="from-address" required error={problems.address ?? undefined}>
                <Input
                  id="from-address"
                  type="email"
                  value={address}
                  onChange={e => setAddress(e.target.value)}
                  placeholder="ops@example.com"
                />
              </Field>
              <Field label="Reply-To" htmlFor="reply-to" error={problems.replyTo ?? undefined} hint="Optional.">
                <Input id="reply-to" type="email" value={replyTo} onChange={e => setReplyTo(e.target.value)} />
              </Field>
              <Field
                label="Console URL for links"
                htmlFor="base-url"
                error={problems.baseUrl ?? undefined}
                hint="Optional. Emails link back here.">
                <Input
                  id="base-url"
                  inputMode="url"
                  value={baseUrl}
                  onChange={e => setBaseUrl(e.target.value)}
                  placeholder="https://vault.example.com"
                />
              </Field>
            </div>

            {provider === 'resend' ?
              <div className="rounded-card border-line bg-surface-1 space-y-3 border p-4">
                <div className="flex flex-wrap items-center justify-between gap-2">
                  <span className="text-fg text-sm font-medium">Resend</span>
                  <Stored value={secrets.resend_api_key} label="API key" />
                </div>
                <Field
                  label={secrets.resend_api_key ? 'Replace API key' : 'API key'}
                  htmlFor="resend-key"
                  hint="Stored in the secrets manager; never shown again.">
                  <Input
                    id="resend-key"
                    type="password"
                    autoComplete="new-password"
                    value={resendKey}
                    onChange={e => setResendKey(e.target.value)}
                    placeholder={secrets.resend_api_key ? 'Unchanged' : 're_…'}
                  />
                </Field>
              </div>
            : <div className="rounded-card border-line bg-surface-1 space-y-3 border p-4">
                <div className="flex flex-wrap items-center justify-between gap-2">
                  <span className="text-fg text-sm font-medium">Amazon SES</span>
                  <span className="flex flex-wrap gap-2">
                    <Stored value={secrets.ses_access_key_id} label="Access key" />
                    <Stored value={secrets.ses_secret_access_key} label="Secret" />
                  </span>
                </div>
                <div className="grid gap-4 sm:grid-cols-2">
                  <Field label="Region" htmlFor="ses-region">
                    <Input
                      id="ses-region"
                      value={region}
                      onChange={e => setRegion(e.target.value)}
                      className="font-mono"
                      placeholder="us-east-1"
                    />
                  </Field>
                  <Field label="Endpoint" htmlFor="ses-endpoint" hint="Optional; the regional default otherwise.">
                    <Input
                      id="ses-endpoint"
                      value={sesEndpoint}
                      onChange={e => setSesEndpoint(e.target.value)}
                      className="font-mono"
                    />
                  </Field>
                  <Field
                    label={secrets.ses_access_key_id ? 'Replace access key ID' : 'Access key ID'}
                    htmlFor="ses-access">
                    <Input
                      id="ses-access"
                      autoComplete="off"
                      value={sesAccess}
                      onChange={e => setSesAccess(e.target.value)}
                      className="font-mono"
                      placeholder={secrets.ses_access_key_id ? 'Unchanged' : undefined}
                    />
                  </Field>
                  <Field
                    label={secrets.ses_secret_access_key ? 'Replace secret access key' : 'Secret access key'}
                    htmlFor="ses-secret">
                    <Input
                      id="ses-secret"
                      type="password"
                      autoComplete="new-password"
                      value={sesSecret}
                      onChange={e => setSesSecret(e.target.value)}
                      placeholder={secrets.ses_secret_access_key ? 'Unchanged' : undefined}
                    />
                  </Field>
                </div>
              </div>
            }
          </>
        }
        <InlineError error={error} />
      </div>
    </Panel>
  )
}

// ---- recipients -------------------------------------------------------------------------------------------------

interface Row {
  email: string
  groups: Record<Group, boolean>
}

const toRows = (recipients: Record<Group, string[]>): Row[] => {
  const map = new Map<string, Row>()
  for (const g of GROUPS)
    for (const email of recipients[g.key]) {
      const row = map.get(email) ?? { email, groups: { alerts: false, weekly: false, security: false } }
      row.groups[g.key] = true
      map.set(email, row)
    }
  return [...map.values()].sort((a, b) => a.email.localeCompare(b.email))
}

const RecipientsPanel = ({ settings }: { settings: EmailSettings }) => {
  const [rows, setRows] = useState<Row[]>(() => toRows(settings.operator.recipients))
  const [draft, setDraft] = useState('')
  const [draftError, setDraftError] = useState<string | null>(null)
  const { busy, error, run } = useSaver()
  const original = JSON.stringify(toRows(settings.operator.recipients))
  const dirty = JSON.stringify(rows) !== original

  const add = () => {
    const email = draft.trim()
    if (!email) return
    if (!EMAIL_RE.test(email)) return setDraftError('Use a full address, e.g. ops@example.com')
    if (rows.some(r => r.email === email)) return setDraftError('Already a recipient')
    setRows(r => [...r, { email, groups: { alerts: true, weekly: true, security: true } }])
    setDraft('')
    setDraftError(null)
  }

  const save = () =>
    run(() =>
      saveConfig(
        {
          operator_emails: {
            recipients: {
              alerts: rows.filter(r => r.groups.alerts).map(r => r.email),
              weekly: rows.filter(r => r.groups.weekly).map(r => r.email),
              security: rows.filter(r => r.groups.security).map(r => r.email),
            },
          },
        },
        'Recipients saved',
      ),
    )

  return (
    <Panel
      id="recipients"
      title="Recipients"
      description="Who receives each kind of operator email."
      actions={
        <Button variant="secondary" onClick={() => void save()} loading={busy} disabled={!dirty}>
          <FloppyDiskIcon aria-hidden />
          Save recipients
        </Button>
      }>
      <div className="space-y-4">
        {rows.length === 0 ?
          <p className="text-fg-subtle text-sm">No recipients yet.</p>
        : <div className="rounded-control border-line overflow-x-auto border">
            <table className="w-full text-sm">
              <thead>
                <tr className="border-line text-fg-subtle border-b text-left text-xs">
                  <th className="h-9 px-3 font-medium">Address</th>
                  {GROUPS.map(g => (
                    <th key={g.key} className="h-9 px-3 text-center font-medium" title={g.hint}>
                      {g.label}
                    </th>
                  ))}
                  <th className="w-10" aria-label="Actions" />
                </tr>
              </thead>
              <tbody>
                {rows.map(row => (
                  <tr key={row.email} className="border-line/60 border-b last:border-0">
                    <td className="text-fg h-10 px-3 break-all">{row.email}</td>
                    {GROUPS.map(g => (
                      <td key={g.key} className="h-10 px-3 text-center">
                        <Checkbox
                          aria-label={`${g.label} for ${row.email}`}
                          checked={row.groups[g.key]}
                          onCheckedChange={on =>
                            setRows(rs =>
                              rs.map(r => (r.email === row.email ? { ...r, groups: { ...r.groups, [g.key]: on } } : r)),
                            )
                          }
                          className="inline-grid"
                        />
                      </td>
                    ))}
                    <td className="px-1 text-right">
                      <Button
                        size="icon-sm"
                        variant="ghost"
                        aria-label={`Remove ${row.email}`}
                        onClick={() => setRows(rs => rs.filter(r => r.email !== row.email))}>
                        <TrashIcon aria-hidden />
                      </Button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        }
        <div className="flex flex-wrap items-start gap-2">
          <Field htmlFor="add-recipient" error={draftError ?? undefined} className="min-w-56 flex-1">
            <Input
              id="add-recipient"
              type="email"
              aria-label="New recipient address"
              placeholder="ops@example.com"
              value={draft}
              onChange={e => {
                setDraft(e.target.value)
                setDraftError(null)
              }}
              onKeyDown={e => {
                if (e.key === 'Enter') {
                  e.preventDefault()
                  add()
                }
              }}
            />
          </Field>
          <Button variant="secondary" onClick={add} disabled={!draft.trim()}>
            <PlusIcon aria-hidden />
            Add
          </Button>
        </div>
        {dirty ?
          <p className="text-fg-subtle text-xs">Unsaved changes.</p>
        : null}
        <InlineError error={error} />
      </div>
    </Panel>
  )
}

// ---- rules ------------------------------------------------------------------------------------------------------

const hourLabel = (h: number) => `${String(h).padStart(2, '0')}:00`

const RulesPanel = ({ settings }: { settings: EmailSettings }) => {
  const op = settings.operator
  const [alerting, setAlerting] = useState(op.alerting)
  const [digest, setDigest] = useState(op.weekly_digest)
  const [security, setSecurity] = useState(op.security_alerts)
  const { busy, error, run } = useSaver()
  const zones = (() => {
    try {
      return (Intl as unknown as { supportedValuesOf?: (k: string) => string[] }).supportedValuesOf?.('timeZone') ?? []
    } catch {
      return []
    }
  })()

  const problems = {
    dedupe: alerting.dedupe_window_minutes < 1 ? 'At least 1 minute' : null,
    repeat: alerting.repeat_after_hours < 1 ? 'At least 1 hour' : null,
    poll: alerting.health_poll_seconds < 15 ? 'At least 15 seconds' : null,
    timezone: digest.timezone.trim() ? null : 'Required',
  }
  const invalid = Object.values(problems).some(Boolean)
  const changed = <T extends object>(a: T, b: T) => JSON.stringify(a) !== JSON.stringify(b)
  const dirty =
    changed(alerting, op.alerting) || changed(digest, op.weekly_digest) || changed(security, op.security_alerts)

  const save = () =>
    run(() =>
      saveConfig(
        {
          operator_emails: {
            ...(changed(alerting, op.alerting) ? { alerting } : {}),
            ...(changed(digest, op.weekly_digest) ?
              { weekly_digest: { ...digest, timezone: digest.timezone.trim() } }
            : {}),
            ...(changed(security, op.security_alerts) ? { security_alerts: security } : {}),
          },
        },
        'Notification rules saved',
      ),
    )

  return (
    <Panel
      id="rules"
      title="What gets sent"
      actions={
        <Button variant="secondary" onClick={() => void save()} loading={busy} disabled={!dirty || invalid}>
          <FloppyDiskIcon aria-hidden />
          Save rules
        </Button>
      }>
      <div className="grid gap-6 lg:grid-cols-3">
        <fieldset className="space-y-3">
          <div className="flex items-center justify-between gap-3">
            <legend className="text-fg text-sm font-medium">Health alerts</legend>
            <Switch
              aria-label="Health alerts"
              checked={alerting.enabled}
              onCheckedChange={v => setAlerting(a => ({ ...a, enabled: v }))}
            />
          </div>
          <Field label="Minimum severity" htmlFor="min-severity">
            <Select
              id="min-severity"
              value={alerting.min_severity}
              onChange={e => setAlerting(a => ({ ...a, min_severity: e.target.value as Severity }))}
              disabled={!alerting.enabled}>
              {SEVERITIES.map(s => (
                <option key={s} value={s}>
                  {titleCase(s)}
                </option>
              ))}
            </Select>
          </Field>
          <Field label="Group repeats within" htmlFor="dedupe" error={problems.dedupe ?? undefined}>
            <UnitInput
              id="dedupe"
              unit="minutes"
              min={1}
              value={alerting.dedupe_window_minutes}
              onChange={v => setAlerting(a => ({ ...a, dedupe_window_minutes: v ?? 0 }))}
              disabled={!alerting.enabled}
            />
          </Field>
          <Field label="Remind again after" htmlFor="repeat" error={problems.repeat ?? undefined}>
            <UnitInput
              id="repeat"
              unit="hours"
              min={1}
              value={alerting.repeat_after_hours}
              onChange={v => setAlerting(a => ({ ...a, repeat_after_hours: v ?? 0 }))}
              disabled={!alerting.enabled}
            />
          </Field>
          <Field label="Check health every" htmlFor="poll" error={problems.poll ?? undefined}>
            <UnitInput
              id="poll"
              unit="seconds"
              min={15}
              value={alerting.health_poll_seconds}
              onChange={v => setAlerting(a => ({ ...a, health_poll_seconds: v ?? 0 }))}
              disabled={!alerting.enabled}
            />
          </Field>
          <label className="text-fg-muted flex items-center gap-2 text-sm">
            <Checkbox
              checked={alerting.send_recovery}
              onCheckedChange={v => setAlerting(a => ({ ...a, send_recovery: v }))}
              disabled={!alerting.enabled}
            />
            Email when an alert clears
          </label>
        </fieldset>

        <fieldset className="space-y-3">
          <div className="flex items-center justify-between gap-3">
            <legend className="text-fg text-sm font-medium">Weekly recap</legend>
            <Switch
              aria-label="Weekly recap"
              checked={digest.enabled}
              onCheckedChange={v => setDigest(d => ({ ...d, enabled: v }))}
            />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <Field label="Day" htmlFor="digest-day">
              <Select
                id="digest-day"
                value={digest.weekday.toLowerCase()}
                onChange={e => setDigest(d => ({ ...d, weekday: e.target.value }))}
                disabled={!digest.enabled}>
                {WEEKDAYS.map(d => (
                  <option key={d} value={d}>
                    {titleCase(d)}
                  </option>
                ))}
              </Select>
            </Field>
            <Field label="Time" htmlFor="digest-hour">
              <Select
                id="digest-hour"
                value={digest.hour_local}
                onChange={e => setDigest(d => ({ ...d, hour_local: Number(e.target.value) }))}
                disabled={!digest.enabled}>
                {Array.from({ length: 24 }, (_, h) => (
                  <option key={h} value={h}>
                    {hourLabel(h)}
                  </option>
                ))}
              </Select>
            </Field>
          </div>
          <Field label="Time zone" htmlFor="digest-tz" error={problems.timezone ?? undefined}>
            <Input
              id="digest-tz"
              list="tz-list"
              value={digest.timezone}
              onChange={e => setDigest(d => ({ ...d, timezone: e.target.value }))}
              disabled={!digest.enabled}
            />
            <datalist id="tz-list">
              {zones.map(z => (
                <option key={z} value={z} />
              ))}
            </datalist>
          </Field>
        </fieldset>

        <fieldset className="space-y-3">
          <div className="flex items-center justify-between gap-3">
            <legend className="text-fg text-sm font-medium">Security notices</legend>
            <Switch
              aria-label="Security notices"
              checked={security.enabled}
              onCheckedChange={v => setSecurity(s => ({ ...s, enabled: v }))}
            />
          </div>
          <label className="text-fg-muted flex items-center gap-2 text-sm">
            <Checkbox
              checked={security.admin_role_changes}
              onCheckedChange={v => setSecurity(s => ({ ...s, admin_role_changes: v }))}
              disabled={!security.enabled}
            />
            Admin role changes
          </label>
        </fieldset>
      </div>
      <InlineError error={error} className="mt-4" />
    </Panel>
  )
}

// ---- test send --------------------------------------------------------------------------------------------------

const TestPanel = ({ settings }: { settings: EmailSettings }) => {
  const all = [...new Set(GROUPS.flatMap(g => settings.operator.recipients[g.key]))]
  const [to, setTo] = useState(all[0] ?? '')
  const [dryRun, setDryRun] = useState(true)
  const [result, setResult] = useState<{
    status: string
    subject: string
    to: string
    text?: string
    warning?: string | null
  } | null>(null)
  const { busy, error, run } = useSaver()
  const valid = EMAIL_RE.test(to.trim())

  const send = () =>
    run(async () => {
      const res = await api.send('email.test.send', { to: to.trim(), dry_run: dryRun })
      setResult({ status: res.status, subject: res.subject, to: res.to, text: res.text, warning: res.history_warning })
      await invalidate('email.history')
    })

  return (
    <Panel
      id="test"
      title="Test"
      description="Render the test email (dry run) or actually send it through the provider.">
      <div className="space-y-4">
        <div className="flex flex-wrap items-end gap-3">
          <Field label="Recipient" htmlFor="test-to" className="min-w-56 flex-1">
            <Input
              id="test-to"
              type="email"
              list="recipient-list"
              value={to}
              onChange={e => setTo(e.target.value)}
              placeholder="you@example.com"
            />
            <datalist id="recipient-list">
              {all.map(a => (
                <option key={a} value={a} />
              ))}
            </datalist>
          </Field>
          <div className="flex h-9 items-center gap-2">
            <Switch id="dry-run" checked={dryRun} onCheckedChange={setDryRun} />
            <Label htmlFor="dry-run">Dry run</Label>
          </div>
          <Button variant="secondary" onClick={() => void send()} loading={busy} disabled={!valid}>
            {dryRun ?
              <FlaskIcon aria-hidden />
            : <PaperPlaneIcon aria-hidden />}
            {dryRun ? 'Render test' : 'Send test email'}
          </Button>
        </div>
        <InlineError error={error} />
        {result ?
          <div className="rounded-card border-line bg-surface-1 space-y-2 border p-3 text-sm">
            <div className="flex flex-wrap items-center gap-2">
              <Badge tone="accent">{result.status === 'dry_run' ? 'Rendered (not sent)' : 'Sent'}</Badge>
              <span className="text-fg">{result.subject}</span>
              <span className="text-fg-subtle">→ {result.to}</span>
            </div>
            {result.warning ?
              <p className="text-fg-subtle text-xs">History not recorded: {result.warning}</p>
            : null}
            {result.text ?
              <pre className="text-fg-muted max-h-64 overflow-auto rounded-md bg-black/40 p-3 font-mono text-xs whitespace-pre-wrap">
                {result.text}
              </pre>
            : null}
          </div>
        : null}
      </div>
    </Panel>
  )
}

// ---- history ----------------------------------------------------------------------------------------------------

const STATUS_TONE: Record<string, 'danger' | 'neutral' | 'accent'> = { failed: 'danger', sent: 'accent' }

const History = ({ enabled }: { enabled: boolean }) => {
  const history = useWs(
    'email.history',
    { limit: 50 },
    { enabled, select: data => (data.history ?? []).map(toHistory) },
  )
  const columns: Column<HistoryRecord>[] = [
    {
      key: 'when',
      header: 'When',
      sortValue: r => r.created_at,
      cell: r => (
        <span className="tabular text-fg-subtle whitespace-nowrap" title={formatDateTime(r.created_at)}>
          {formatRelative(r.created_at)}
        </span>
      ),
    },
    {
      key: 'subject',
      header: 'Email',
      cell: r => (
        <div className="min-w-0 py-1.5">
          <div className="text-fg">{r.subject ?? DASH}</div>
          {r.error_summary ?
            <div className={cn('max-w-xl text-xs', r.status === 'failed' ? 'text-danger' : 'text-fg-subtle')}>
              {r.error_summary}
            </div>
          : null}
        </div>
      ),
    },
    {
      key: 'status',
      header: 'Status',
      cell: r =>
        r.status ?
          <Badge tone={STATUS_TONE[r.status] ?? 'neutral'}>{titleCase(r.status)}</Badge>
        : <span className="text-fg-faint">{DASH}</span>,
    },
    {
      key: 'type',
      header: 'Type',
      hideBelow: 'md',
      cell: r => <span className="text-fg-muted">{r.event_type ? titleCase(r.event_type) : DASH}</span>,
    },
    {
      key: 'to',
      header: 'To',
      hideBelow: 'lg',
      cell: r => (
        <span className="tabular text-fg-subtle">
          {r.recipient_group ? titleCase(r.recipient_group) : DASH}
          {r.recipient_count !== null ? ` · ${r.recipient_count}` : ''}
        </span>
      ),
    },
    {
      key: 'provider',
      header: 'Provider',
      hideBelow: 'lg',
      cell: r => <span className="text-fg-subtle">{r.provider ?? DASH}</span>,
    },
  ]
  return (
    <Section
      id="history"
      title="History"
      description="The last 50 operator emails, including suppressed and failed ones.">
      <QueryState query={history}>
        {rows =>
          rows.length === 0 ?
            <EmptyRows>No operator email has been sent yet.</EmptyRows>
          : <DataTable rows={rows} columns={columns} rowKey={r => r.id} initialSort={{ key: 'when', dir: 'desc' }} />
        }
      </QueryState>
    </Section>
  )
}

export const NotificationsPage = () => {
  const allowed = useIsSuperAdmin()
  const config = useWs('email.config.get', null, { enabled: allowed, select: data => toSettings(data) })
  return (
    <>
      <PageHeader
        title="Notifications"
        description="Operator email: alerts, the weekly recap and security notices, who gets them, and how they’re sent."
      />
      {!allowed ?
        <EmptyState
          className="panel"
          title="You don't have access to this"
          description="Operator email is managed by super admins."
        />
      : <QueryState query={config}>
          {settings => {
            // Remount the editors whenever the saved config changes so they start from the server's state.
            const version = JSON.stringify(settings)
            return (
              <div className="space-y-6">
                <ProviderPanel key={`p${version}`} settings={settings} />
                <RecipientsPanel key={`r${version}`} settings={settings} />
                <RulesPanel key={`a${version}`} settings={settings} />
                <TestPanel settings={settings} />
                <History enabled={allowed} />
              </div>
            )
          }}
        </QueryState>
      }
    </>
  )
}
