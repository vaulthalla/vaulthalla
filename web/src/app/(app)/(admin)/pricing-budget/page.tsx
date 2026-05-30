'use client'

import React, { useEffect, useMemo, useState } from 'react'
import Link from 'next/link'
import { AdminPage } from '@/components/admin/AdminPage'
import CircleNotchIcon from '@/fa-duotone/circle-notch.svg'
import CheckIcon from '@/fa-duotone/circle-check.svg'
import RefreshIcon from '@/fa-duotone/arrows-rotate.svg'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import ShieldIcon from '@/fa-duotone/shield-check.svg'
import WarningIcon from '@/fa-duotone/triangle-exclamation.svg'
import XIcon from '@/fa-duotone/circle-xmark.svg'
import {
  PriceBudgetMode,
  PriceBudgetPolicy,
  PriceBudgetPolicyPayload,
  PriceBudgetPreflightResult,
  PriceBudgetScope,
  PriceBudgetTrendStats,
} from '@/models/pricing/priceBudget'
import { PriceNotification } from '@/models/pricing/priceNotification'
import { PriceOverride } from '@/models/pricing/priceOverride'
import { PricingBudgetStats } from '@/models/stats/pricingBudgetStats'
import { S3Vault } from '@/models/vaults'
import { usePricingStore } from '@/stores/pricingStore'
import { useVaultStore } from '@/stores/vaultStore'

const providers = [
  { key: 'aws-s3', label: 'AWS S3' },
  { key: 'cloudflare-r2', label: 'Cloudflare R2' },
]

const modes: PriceBudgetMode[] = ['off', 'report', 'warn', 'enforce']

type PolicyTarget = {
  scope: PriceBudgetScope
  title: string
  provider_key?: string | null
  vault_id?: number | null
}

const money = (value: string | number | null | undefined, currency = 'USD') => {
  const amount = Number(value ?? 0)
  if (!Number.isFinite(amount)) return `${value ?? '0'} ${currency}`
  return new Intl.NumberFormat(undefined, { style: 'currency', currency, maximumFractionDigits: 4 }).format(amount)
}

const compactNumber = (value: number) => new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 }).format(value)

const formatWhen = (value: number | string | null) => {
  if (value == null) return ''
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return String(value)
  return date.toLocaleString()
}

const parseLimit = (value: string) => {
  const trimmed = value.trim()
  return trimmed.length > 0 ? trimmed : null
}

const sectionClass = 'rounded border border-white/10 bg-zinc-950/65 text-white shadow-xl'

function newPolicyForTarget(target: PolicyTarget): PriceBudgetPolicyPayload {
  return {
    scope: target.scope,
    provider_key: target.provider_key ?? null,
    vault_id: target.vault_id ?? null,
    mode: 'off',
    currency: 'USD',
    max_run_cost: null,
    max_daily_cost: null,
    max_monthly_cost: null,
    require_verified_catalog: true,
    allow_stale_catalog: false,
    max_catalog_age_seconds: 43200,
  }
}

function sameTarget(policy: PriceBudgetPolicy, target: PolicyTarget) {
  if (policy.scope !== target.scope) return false
  if ((policy.provider_key ?? null) !== (target.provider_key ?? null)) return false
  if ((policy.vault_id ?? null) !== (target.vault_id ?? null)) return false
  return true
}

function Section({ title, children, right }: { title: string; children: React.ReactNode; right?: React.ReactNode }) {
  return (
    <section className={sectionClass}>
      <header className="flex flex-wrap items-center justify-between gap-3 border-b border-white/10 px-4 py-3">
        <h2 className="text-sm font-semibold uppercase tracking-normal text-cyan-100">{title}</h2>
        {right}
      </header>
      <div className="p-4">{children}</div>
    </section>
  )
}

function Metric({ label, value, tone = 'neutral' }: { label: string; value: React.ReactNode; tone?: 'neutral' | 'good' | 'warn' | 'bad' }) {
  const toneClass =
    tone === 'good' ? 'text-emerald-200'
    : tone === 'warn' ? 'text-amber-200'
    : tone === 'bad' ? 'text-red-200'
    : 'text-white'
  return (
    <div className="rounded border border-white/10 bg-white/[0.03] p-3">
      <div className="text-xs uppercase tracking-normal text-white/45">{label}</div>
      <div className={`mt-1 text-xl font-semibold ${toneClass}`}>{value}</div>
    </div>
  )
}

function PolicyEditor({
  target,
  policies,
  disabled,
  onSave,
  onDisable,
}: {
  target: PolicyTarget
  policies: PriceBudgetPolicy[]
  disabled?: boolean
  onSave: (payload: PriceBudgetPolicyPayload) => Promise<void>
  onDisable: (target: PolicyTarget) => Promise<void>
}) {
  const existing = useMemo(() => policies.find(policy => sameTarget(policy, target)), [policies, target])
  const [mode, setMode] = useState<PriceBudgetMode>('off')
  const [currency, setCurrency] = useState('USD')
  const [runLimit, setRunLimit] = useState('')
  const [dailyLimit, setDailyLimit] = useState('')
  const [monthlyLimit, setMonthlyLimit] = useState('')
  const [requireVerified, setRequireVerified] = useState(true)
  const [allowStale, setAllowStale] = useState(false)
  const [maxAge, setMaxAge] = useState('43200')
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    const base = existing ?? new PriceBudgetPolicy(newPolicyForTarget(target))
    setMode(base.mode)
    setCurrency(base.currency)
    setRunLimit(base.max_run_cost ?? '')
    setDailyLimit(base.max_daily_cost ?? '')
    setMonthlyLimit(base.max_monthly_cost ?? '')
    setRequireVerified(base.require_verified_catalog)
    setAllowStale(base.allow_stale_catalog)
    setMaxAge(String(base.max_catalog_age_seconds ?? 43200))
  }, [existing, target])

  const save = async () => {
    setSaving(true)
    try {
      await onSave({
        ...newPolicyForTarget(target),
        mode,
        currency: currency || 'USD',
        max_run_cost: parseLimit(runLimit),
        max_daily_cost: parseLimit(dailyLimit),
        max_monthly_cost: parseLimit(monthlyLimit),
        require_verified_catalog: requireVerified,
        allow_stale_catalog: allowStale,
        max_catalog_age_seconds: maxAge.trim() ? Number(maxAge) : null,
      })
    } finally {
      setSaving(false)
    }
  }

  const disablePolicy = async () => {
    setSaving(true)
    try {
      await onDisable(target)
    } finally {
      setSaving(false)
    }
  }

  return (
    <div className="rounded border border-white/10 bg-white/[0.03] p-3">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <div className="font-medium text-white">{target.title}</div>
          <div className="mt-1 text-xs text-white/45">
            {existing?.is_active ? 'Active clamp' : 'No active clamp'} · {target.scope}
          </div>
        </div>
        <span className="rounded border border-white/10 px-2 py-1 text-xs text-white/60">{target.provider_key ?? 'combined S3'}</span>
      </div>

      <div className="mt-4 grid gap-3 md:grid-cols-4">
        <label className="flex flex-col gap-1 text-xs text-white/60">
          Mode
          <select
            className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            disabled={disabled || saving}
            value={mode}
            onChange={event => setMode(event.target.value as PriceBudgetMode)}>
            {modes.map(item => (
              <option key={item} value={item}>
                {item}
              </option>
            ))}
          </select>
        </label>
        <label className="flex flex-col gap-1 text-xs text-white/60">
          Currency
          <input
            className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            disabled={disabled || saving}
            value={currency}
            onChange={event => setCurrency(event.target.value.toUpperCase())}
          />
        </label>
        <label className="flex flex-col gap-1 text-xs text-white/60">
          Max run cost
          <input
            className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            disabled={disabled || saving}
            inputMode="decimal"
            value={runLimit}
            onChange={event => setRunLimit(event.target.value)}
          />
        </label>
        <label className="flex flex-col gap-1 text-xs text-white/60">
          Max daily cost
          <input
            className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            disabled={disabled || saving}
            inputMode="decimal"
            value={dailyLimit}
            onChange={event => setDailyLimit(event.target.value)}
          />
        </label>
        <label className="flex flex-col gap-1 text-xs text-white/60">
          Max monthly cost
          <input
            className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            disabled={disabled || saving}
            inputMode="decimal"
            value={monthlyLimit}
            onChange={event => setMonthlyLimit(event.target.value)}
          />
        </label>
        <label className="flex flex-col gap-1 text-xs text-white/60">
          Max catalog age seconds
          <input
            className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            disabled={disabled || saving}
            inputMode="numeric"
            value={maxAge}
            onChange={event => setMaxAge(event.target.value)}
          />
        </label>
        <label className="flex min-h-10 items-center justify-between gap-3 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white/75 md:mt-5">
          Require verified catalog
          <input
            className="h-4 w-4 accent-cyan-400"
            checked={requireVerified}
            disabled={disabled || saving}
            type="checkbox"
            onChange={event => setRequireVerified(event.target.checked)}
          />
        </label>
        <label className="flex min-h-10 items-center justify-between gap-3 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white/75 md:mt-5">
          Allow stale catalog
          <input
            className="h-4 w-4 accent-cyan-400"
            checked={allowStale}
            disabled={disabled || saving}
            type="checkbox"
            onChange={event => setAllowStale(event.target.checked)}
          />
        </label>
      </div>

      <div className="mt-4 flex flex-wrap justify-end gap-2">
        <button
          className="inline-flex min-h-10 items-center gap-2 rounded border border-white/10 px-3 py-2 text-sm text-white/70 hover:bg-white/10 disabled:opacity-40"
          disabled={disabled || saving || !existing?.is_active}
          type="button"
          onClick={() => void disablePolicy()}>
          <XIcon className="h-4 w-4 fill-current" />
          Disable
        </button>
        <button
          className="inline-flex min-h-10 items-center gap-2 rounded border border-cyan-400/40 bg-cyan-400/10 px-3 py-2 text-sm text-cyan-100 hover:bg-cyan-400/20 disabled:opacity-40"
          disabled={disabled || saving}
          type="button"
          onClick={() => void save()}>
          <SaveIcon className="h-4 w-4 fill-current" />
          Save
        </button>
      </div>
    </div>
  )
}

function StatsOverview({ stats }: { stats: PricingBudgetStats | null }) {
  return (
    <div className="grid gap-3 md:grid-cols-3 xl:grid-cols-6">
      <Metric label="Active policies" value={stats?.active_policies ?? 0} />
      <Metric label="Blocked 24h" value={stats?.blocked_syncs_24h ?? 0} tone={(stats?.blocked_syncs_24h ?? 0) > 0 ? 'bad' : 'neutral'} />
      <Metric label="Warnings" value={stats?.warning_notifications ?? 0} tone={(stats?.warning_notifications ?? 0) > 0 ? 'warn' : 'neutral'} />
      <Metric label="Critical" value={stats?.critical_notifications ?? 0} tone={(stats?.critical_notifications ?? 0) > 0 ? 'bad' : 'neutral'} />
      <Metric label="Month spend" value={money(stats?.current_monthly_spend, stats?.currency)} />
      <Metric label="Projection" value={money(stats?.projected_monthly_spend, stats?.currency)} tone="warn" />
    </div>
  )
}

function TrendTable({ trends }: { trends: PriceBudgetTrendStats[] }) {
  if (!trends.length) return <div className="rounded border border-white/10 bg-white/[0.03] p-4 text-sm text-white/50">No budget trend data yet.</div>
  return (
    <div className="overflow-x-auto">
      <table className="w-full min-w-[54rem] text-left text-sm">
        <thead className="text-xs uppercase tracking-normal text-white/45">
          <tr className="border-b border-white/10">
            <th className="py-2 pr-3">Scope</th>
            <th className="py-2 pr-3">Window</th>
            <th className="py-2 pr-3">Spend</th>
            <th className="py-2 pr-3">Limit</th>
            <th className="py-2 pr-3">Used</th>
            <th className="py-2 pr-3">Projected</th>
            <th className="py-2 pr-3">Confidence</th>
          </tr>
        </thead>
        <tbody>
          {trends.map((trend, index) => (
            <tr key={`${trend.policy_id}-${trend.window_type}-${index}`} className="border-b border-white/5 last:border-b-0">
              <td className="py-2 pr-3 text-white">
                {trend.scope}
                <div className="text-xs text-white/40">{trend.provider_key ?? (trend.vault_id ? `Vault ${trend.vault_id}` : 'combined')}</div>
              </td>
              <td className="py-2 pr-3 text-white/70">{trend.window_type}</td>
              <td className="py-2 pr-3 text-white/70">{money(trend.total_cost, trend.currency)}</td>
              <td className="py-2 pr-3 text-white/70">{trend.limit ? money(trend.limit, trend.currency) : '-'}</td>
              <td className="py-2 pr-3 text-white/70">{compactNumber(trend.percent_used)}%</td>
              <td className="py-2 pr-3 text-white/70">{trend.projected_window_cost ? money(trend.projected_window_cost, trend.currency) : '-'}</td>
              <td className="py-2 pr-3 text-white/70">{trend.confidence}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function BlockedPrompt({
  preflight,
  selectedVaultId,
  requesting,
  onRequest,
  onRetry,
}: {
  preflight: PriceBudgetPreflightResult
  selectedVaultId: number
  requesting: boolean
  onRequest: (reason: string) => Promise<void>
  onRetry: () => Promise<void>
}) {
  const [reason, setReason] = useState('')
  const decision = preflight.decision
  const exceededPolicies = decision.checks
    .filter(check => check.exceeded && check.mode === 'enforce' && check.policy_id != null)
    .map(check => check.policy_id)

  if (!decision.stalled) return null

  return (
    <div className="rounded border border-red-500/40 bg-red-500/10 p-4">
      <div className="flex items-start gap-3">
        <WarningIcon className="mt-1 h-5 w-5 shrink-0 fill-current text-red-200" />
        <div className="min-w-0 flex-1">
          <div className="font-semibold text-red-100">Sync blocked by S3 price budget</div>
          <p className="mt-1 text-sm text-red-100/75">{decision.reason || 'The projected S3 cost exceeds an enforce-mode policy.'}</p>
          <div className="mt-3 grid gap-2 text-sm text-white/75 md:grid-cols-3">
            <div>Vault {selectedVaultId}</div>
            <div>Requested {money(decision.requested, decision.currency)}</div>
            <div>Remaining {money(decision.remaining_before, decision.currency)}</div>
            <div>Limit {money(decision.limit, decision.currency)}</div>
            <div>Catalog {preflight.estimate.catalog_verified ? 'verified' : 'unverified'}</div>
            <div>Confidence {preflight.estimate.confidence_level ?? 'unknown'}</div>
          </div>
          <textarea
            className="mt-3 min-h-20 w-full rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
            placeholder="Override reason"
            value={reason}
            onChange={event => setReason(event.target.value)}
          />
          <div className="mt-3 flex flex-wrap gap-2">
            <button
              className="inline-flex min-h-10 items-center gap-2 rounded border border-amber-300/40 bg-amber-300/10 px-3 py-2 text-sm text-amber-100 hover:bg-amber-300/20 disabled:opacity-40"
              disabled={requesting || exceededPolicies.length === 0}
              type="button"
              onClick={() => void onRequest(reason)}>
              <ShieldIcon className="h-4 w-4 fill-current" />
              Request override
            </button>
            <button
              className="inline-flex min-h-10 items-center gap-2 rounded border border-white/10 px-3 py-2 text-sm text-white/70 hover:bg-white/10"
              type="button"
              onClick={() => void onRetry()}>
              <RefreshIcon className="h-4 w-4 fill-current" />
              Retry sync
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

function NotificationRows({ notifications, onAck }: { notifications: PriceNotification[]; onAck: (notification: PriceNotification) => Promise<void> }) {
  if (!notifications.length) return <div className="rounded border border-white/10 bg-white/[0.03] p-4 text-sm text-white/50">No active budget notifications.</div>
  return (
    <div className="space-y-2">
      {notifications.map(notification => (
        <div key={notification.id} className="rounded border border-white/10 bg-white/[0.03] p-3">
          <div className="flex flex-wrap items-start justify-between gap-3">
            <div className="min-w-0">
              <div className="flex flex-wrap items-center gap-2">
                <span className="rounded border border-white/10 px-2 py-0.5 text-xs uppercase tracking-normal text-white/60">{notification.severity}</span>
                <span className="font-medium text-white">{notification.title}</span>
              </div>
              <p className="mt-1 text-sm text-white/60">{notification.message}</p>
              <div className="mt-2 flex flex-wrap gap-2 text-xs text-white/40">
                {notification.vault_id ? <Link className="text-cyan-200 hover:underline" href={`/vaults/${notification.vault_id}`}>Vault {notification.vault_id}</Link> : null}
                {notification.policy_id ? <span>Policy {notification.policy_id}</span> : null}
                {notification.run_uuid ? <span>Run {notification.run_uuid}</span> : null}
                <span>{formatWhen(notification.created_at)}</span>
              </div>
            </div>
            <button
              className="inline-flex min-h-9 items-center gap-2 rounded border border-white/10 px-3 py-2 text-xs text-white/70 hover:bg-white/10"
              type="button"
              onClick={() => void onAck(notification)}>
              <CheckIcon className="h-3.5 w-3.5 fill-current" />
              Ack
            </button>
          </div>
        </div>
      ))}
    </div>
  )
}

function OverrideRows({
  overrides,
  onApprove,
  onDeny,
}: {
  overrides: PriceOverride[]
  onApprove: (id: number) => Promise<void>
  onDeny: (id: number) => Promise<void>
}) {
  if (!overrides.length) return <div className="rounded border border-white/10 bg-white/[0.03] p-4 text-sm text-white/50">No override requests.</div>
  return (
    <div className="space-y-2">
      {overrides.map(override => (
        <div key={override.id} className="rounded border border-white/10 bg-white/[0.03] p-3">
          <div className="flex flex-wrap items-start justify-between gap-3">
            <div>
              <div className="font-medium text-white">
                Override #{override.id} · <span className="text-cyan-200">{override.status}</span>
              </div>
              <div className="mt-1 flex flex-wrap gap-2 text-sm text-white/55">
                <span>Vault {override.vault_id}</span>
                <span>{override.estimated_cost ? override.estimatedCostText() : 'No estimate'}</span>
                <span>Expires {formatWhen(override.expires_at)}</span>
              </div>
              {override.reason ? <p className="mt-2 text-sm text-white/65">{override.reason}</p> : null}
            </div>
            {override.status === 'requested' ?
              <div className="flex gap-2">
                <button
                  className="inline-flex min-h-9 items-center gap-2 rounded border border-emerald-400/40 bg-emerald-400/10 px-3 py-2 text-xs text-emerald-100 hover:bg-emerald-400/20"
                  type="button"
                  onClick={() => void onApprove(override.id)}>
                  <CheckIcon className="h-3.5 w-3.5 fill-current" />
                  Approve
                </button>
                <button
                  className="inline-flex min-h-9 items-center gap-2 rounded border border-red-400/40 bg-red-400/10 px-3 py-2 text-xs text-red-100 hover:bg-red-400/20"
                  type="button"
                  onClick={() => void onDeny(override.id)}>
                  <XIcon className="h-3.5 w-3.5 fill-current" />
                  Deny
                </button>
              </div>
            : null}
          </div>
        </div>
      ))}
    </div>
  )
}

export default function PricingBudgetPage() {
  const policies = usePricingStore(state => state.policies)
  const notifications = usePricingStore(state => state.notifications)
  const overrides = usePricingStore(state => state.overrides)
  const status = usePricingStore(state => state.status)
  const stats = usePricingStore(state => state.stats)
  const pricingLoading = usePricingStore(state => state.loading)
  const pricingError = usePricingStore(state => state.error)
  const fetchStatus = usePricingStore(state => state.fetchStatus)
  const fetchStats = usePricingStore(state => state.fetchStats)
  const upsertPolicy = usePricingStore(state => state.upsertPolicy)
  const disableBudgetPolicy = usePricingStore(state => state.disablePolicy)
  const preflightBudget = usePricingStore(state => state.preflight)
  const requestBudgetOverride = usePricingStore(state => state.requestOverride)
  const approveBudgetOverride = usePricingStore(state => state.approveOverride)
  const denyBudgetOverride = usePricingStore(state => state.denyOverride)
  const ackNotification = usePricingStore(state => state.ackNotification)
  const vaults = useVaultStore(state => state.vaults)
  const fetchVaults = useVaultStore(state => state.fetchVaults)
  const syncVault = useVaultStore(state => state.syncVault)
  const [selectedVaultId, setSelectedVaultId] = useState<number | null>(null)
  const [preflight, setPreflight] = useState<PriceBudgetPreflightResult | null>(null)
  const [preflightError, setPreflightError] = useState<string | null>(null)
  const [requestingOverride, setRequestingOverride] = useState(false)

  const s3Vaults = useMemo(() => vaults.filter((vault): vault is S3Vault => vault.type === 's3').map(vault => new S3Vault(vault)), [vaults])
  const selectedVault = useMemo(() => s3Vaults.find(vault => vault.id === selectedVaultId) ?? null, [s3Vaults, selectedVaultId])
  const inheritedPolicies = useMemo(
    () => policies.filter(policy => policy.scope !== 'vault' && policy.is_active),
    [policies],
  )

  useEffect(() => {
    void fetchVaults().catch(() => undefined)
    void fetchStatus({ limit: 50, include_inactive: true }).catch(() => undefined)
    void fetchStats().catch(() => undefined)
  }, [fetchStats, fetchStatus, fetchVaults])

  useEffect(() => {
    if (!selectedVaultId && s3Vaults.length > 0) setSelectedVaultId(s3Vaults[0].id)
  }, [s3Vaults, selectedVaultId])

  const refresh = async () => {
    await fetchStatus(selectedVaultId ? { vault_id: selectedVaultId, limit: 50, include_inactive: true } : { limit: 50, include_inactive: true })
    await fetchStats(selectedVaultId ? { vault_id: selectedVaultId } : undefined)
  }

  const savePolicy = async (payload: PriceBudgetPolicyPayload) => {
    await upsertPolicy(payload)
    await refresh()
  }

  const disablePolicy = async (target: PolicyTarget) => {
    await disableBudgetPolicy({ scope: target.scope, provider_key: target.provider_key, vault_id: target.vault_id })
    await refresh()
  }

  const runPreflight = async () => {
    if (!selectedVaultId) return
    setPreflightError(null)
    try {
      const result = await preflightBudget({ vault_id: selectedVaultId })
      setPreflight(result)
    } catch (error) {
      setPreflight(null)
      setPreflightError(error instanceof Error ? error.message : 'Unable to run S3 price budget preflight')
    }
  }

  const requestOverride = async (reason: string) => {
    if (!selectedVaultId || !preflight) return
    const policyIds = preflight.decision.checks
      .filter(check => check.exceeded && check.mode === 'enforce' && check.policy_id != null)
      .map(check => check.policy_id)
      .filter((id): id is number => id != null)
    setRequestingOverride(true)
    try {
      await requestBudgetOverride({
        vault_id: selectedVaultId,
        reason,
        policy_ids: policyIds,
        estimated_cost: preflight.estimate.estimated_cost,
        currency: preflight.estimate.currency ?? preflight.decision.currency,
      })
      await refresh()
    } finally {
      setRequestingOverride(false)
    }
  }

  const approveOverride = async (id: number) => {
    await approveBudgetOverride(id)
    await refresh()
  }

  const denyOverride = async (id: number) => {
    await denyBudgetOverride(id)
    await refresh()
  }

  const retrySync = async () => {
    if (!selectedVaultId) return
    await syncVault({ id: selectedVaultId })
    await refresh().catch(() => undefined)
  }

  const vaultTarget: PolicyTarget | null = selectedVault ?
    { scope: 'vault', title: `${selectedVault.name} vault policy`, vault_id: selectedVault.id }
  : null

  return (
    <AdminPage
      title="Cost Control"
      description="S3 price budget policies, dry-run decisions, notifications, overrides, and spend projection.">
      <div className="mt-6 space-y-4">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div className="flex flex-wrap items-center gap-2">
            <button
              className="inline-flex min-h-10 items-center gap-2 rounded border border-white/10 bg-white/5 px-3 py-2 text-sm text-white/70 hover:bg-white/10"
              type="button"
              onClick={() => void refresh()}>
              <RefreshIcon className="h-4 w-4 fill-current" />
              Refresh
            </button>
            {pricingLoading ? <CircleNotchIcon className="h-5 w-5 animate-spin fill-current text-cyan-300" /> : null}
          </div>
          {pricingError ? <div className="rounded border border-red-500/40 bg-red-500/10 px-3 py-2 text-sm text-red-100">{pricingError}</div> : null}
        </div>

        <StatsOverview stats={stats} />

        <div className="grid gap-4 xl:grid-cols-[minmax(0,1.2fr)_minmax(24rem,0.8fr)]">
          <Section title="System Policies">
            <div className="space-y-3">
              <PolicyEditor
                policies={policies}
                target={{ scope: 'global', title: 'Global combined S3 budget' }}
                onDisable={disablePolicy}
                onSave={savePolicy}
              />
              {providers.map(provider => (
                <PolicyEditor
                  key={provider.key}
                  policies={policies}
                  target={{ scope: 'provider', title: `${provider.label} provider budget`, provider_key: provider.key }}
                  onDisable={disablePolicy}
                  onSave={savePolicy}
                />
              ))}
            </div>
          </Section>

          <Section title="Provider Coverage">
            <div className="space-y-3">
              {providers.map(provider => (
                <div key={provider.key} className="flex items-center justify-between gap-3 rounded border border-white/10 bg-white/[0.03] p-3">
                  <div>
                    <div className="font-medium text-white">{provider.label}</div>
                    <div className="text-xs text-white/45">{provider.key}</div>
                  </div>
                  <span className="rounded border border-emerald-400/40 bg-emerald-400/10 px-2 py-1 text-xs text-emerald-100">Budget supported</span>
                </div>
              ))}
            </div>
          </Section>
        </div>

        <div className="grid gap-4 xl:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]">
          <Section
            title="Vault Policy"
            right={
              <select
                className="min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400"
                value={selectedVaultId ?? ''}
                onChange={event => setSelectedVaultId(event.target.value ? Number(event.target.value) : null)}>
                {s3Vaults.map(vault => (
                  <option key={vault.id} value={vault.id}>
                    {vault.name}
                  </option>
                ))}
              </select>
            }>
            {vaultTarget ?
              <div className="space-y-4">
                <PolicyEditor policies={policies} target={vaultTarget} onDisable={disablePolicy} onSave={savePolicy} />
                <div className="rounded border border-cyan-400/20 bg-cyan-400/5 p-3">
                  <div className="text-sm font-medium text-cyan-100">Inherited clamps</div>
                  <div className="mt-2 space-y-1 text-sm text-white/60">
                    {inheritedPolicies.length ?
                      inheritedPolicies.map(policy => (
                        <div key={`${policy.scope}-${policy.provider_key ?? 'global'}`}>
                          {policy.scope} {policy.provider_key ?? 'combined'} · {policy.mode} · monthly{' '}
                          {policy.max_monthly_cost ? money(policy.max_monthly_cost, policy.currency) : 'unlimited'}
                        </div>
                      ))
                    : <div>No inherited global or provider policies are active.</div>}
                  </div>
                  <div className="mt-2 text-xs text-white/40">Vault, provider, and global scopes all clamp the same sync attempt.</div>
                </div>
              </div>
            : <div className="rounded border border-white/10 bg-white/[0.03] p-4 text-sm text-white/50">No S3 vaults are available.</div>}
          </Section>

          <Section title="Dry-Run Decision">
            <div className="space-y-3">
              <button
                className="inline-flex min-h-10 items-center gap-2 rounded border border-cyan-400/40 bg-cyan-400/10 px-3 py-2 text-sm text-cyan-100 hover:bg-cyan-400/20 disabled:opacity-40"
                disabled={!selectedVaultId}
                type="button"
                onClick={() => void runPreflight()}>
                <RefreshIcon className="h-4 w-4 fill-current" />
                Run preflight
              </button>
              {preflightError ? <div className="rounded border border-red-500/40 bg-red-500/10 p-3 text-sm text-red-100">{preflightError}</div> : null}
              {preflight ?
                <div className="space-y-3">
                  <div className="grid gap-2 md:grid-cols-3">
                    <Metric label="Decision" value={preflight.decision.allowed ? 'Allowed' : 'Blocked'} tone={preflight.decision.allowed ? 'good' : 'bad'} />
                    <Metric label="Estimated cost" value={money(preflight.estimate.estimated_cost, preflight.estimate.currency ?? 'USD')} />
                    <Metric label="Warnings" value={preflight.decision.warnings.length} tone={preflight.decision.warnings.length ? 'warn' : 'neutral'} />
                  </div>
                  <BlockedPrompt
                    preflight={preflight}
                    requesting={requestingOverride}
                    selectedVaultId={selectedVaultId ?? 0}
                    onRequest={requestOverride}
                    onRetry={retrySync}
                  />
                </div>
              : null}
            </div>
          </Section>
        </div>

        <div className="grid gap-4 xl:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]">
          <Section title="Spend Trends">
            <TrendTable trends={status?.trends ?? []} />
          </Section>

          <Section title="Recent Ledger">
            <div className="space-y-2">
              {(status?.ledger ?? []).slice(0, 8).map(entry => (
                <div key={entry.id ?? `${entry.run_uuid}-${entry.created_at}`} className="rounded border border-white/10 bg-white/[0.03] p-3 text-sm">
                  <div className="flex flex-wrap items-center justify-between gap-2">
                    <div className="font-medium text-white">{entry.provider_key}</div>
                    <div className="text-white/60">{money(entry.committed_cost ?? entry.reserved_cost, entry.currency)}</div>
                  </div>
                  <div className="mt-1 flex flex-wrap gap-2 text-xs text-white/40">
                    <span>{entry.window}</span>
                    <span>{entry.status}</span>
                    <span>Vault {entry.vault_id}</span>
                    <span>{formatWhen(entry.created_at)}</span>
                  </div>
                </div>
              ))}
              {(status?.ledger ?? []).length === 0 ?
                <div className="rounded border border-white/10 bg-white/[0.03] p-4 text-sm text-white/50">No ledger rows yet.</div>
              : null}
            </div>
          </Section>
        </div>

        <div className="grid gap-4 xl:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]">
          <Section title="Budget Alerts">
            <NotificationRows
              notifications={notifications}
              onAck={async notification => {
                await ackNotification({ id: notification.id, vault_id: notification.vault_id })
              }}
            />
          </Section>

          <Section title="Overrides">
            <OverrideRows overrides={overrides} onApprove={approveOverride} onDeny={denyOverride} />
          </Section>
        </div>

        <div className="rounded border border-white/10 bg-zinc-950/65 p-4 text-xs text-white/45">
          Price budgets remain off until an operator saves a policy with report, warn, or enforce mode. Dashboard polling reads local budget state only.
        </div>
      </div>
    </AdminPage>
  )
}
