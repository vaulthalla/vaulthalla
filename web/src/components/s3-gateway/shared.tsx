'use client'

import React from 'react'
import { PriceBudgetMode, PriceBudgetPolicyPayload, PriceBudgetTrendStats } from '@/models/pricing/priceBudget'
import { S3GatewayCredentialScopeMode, S3GatewayCredentialVaultScopePayload } from '@/models/s3Gateway'

export const cardClass = 'rounded border border-white/10 bg-zinc-950/65 text-white shadow-xl'
export const fieldClass = 'min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400'
export const buttonClass = 'inline-flex min-h-10 items-center justify-center gap-2 rounded border border-white/10 bg-white/[0.06] px-3 py-2 text-sm text-white hover:bg-white/[0.1] disabled:cursor-not-allowed disabled:opacity-50'
export const primaryButtonClass = 'inline-flex min-h-10 items-center justify-center gap-2 rounded bg-cyan-500 px-3 py-2 text-sm font-medium text-zinc-950 hover:bg-cyan-300 disabled:cursor-not-allowed disabled:opacity-50'
export const dangerButtonClass = 'inline-flex min-h-10 items-center justify-center gap-2 rounded border border-red-300/25 bg-red-500/10 px-3 py-2 text-sm text-red-100 hover:bg-red-500/20 disabled:cursor-not-allowed disabled:opacity-50'

export const modes: PriceBudgetMode[] = ['report', 'warn', 'enforce']
export const scopeModes: S3GatewayCredentialScopeMode[] = ['user_access', 'vault_allowlist', 'global']
export const permissionKeys: (keyof Omit<S3GatewayCredentialVaultScopePayload, 'vault_id'>)[] = [
  'can_list',
  'can_read',
  'can_write',
  'can_delete',
  'can_admin',
]

export const formatDate = (value: number | string | null | undefined) => {
  if (value == null) return '-'
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? String(value) : date.toLocaleString()
}

export const money = (value: string | number | null | undefined, currency = 'USD') => {
  const amount = Number(value ?? 0)
  if (!Number.isFinite(amount)) return `${value ?? '0'} ${currency}`
  return new Intl.NumberFormat(undefined, { style: 'currency', currency, maximumFractionDigits: 4 }).format(amount)
}

export const percent = (value: number) => new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 }).format(value * 100)
export const scopeLabel = (scope: S3GatewayCredentialScopeMode) => scope.replace('_', '-')

export function Section({ title, icon: Icon, right, children }: {
  title: string
  icon: React.ComponentType<React.SVGProps<SVGSVGElement>>
  right?: React.ReactNode
  children: React.ReactNode
}) {
  return (
    <section className={cardClass}>
      <header className="flex flex-wrap items-center justify-between gap-3 border-b border-white/10 px-4 py-3">
        <h2 className="flex items-center gap-2 text-sm font-semibold uppercase tracking-normal text-cyan-100">
          <Icon className="h-4 w-4" />
          {title}
        </h2>
        {right}
      </header>
      <div className="p-4">{children}</div>
    </section>
  )
}

export function Metric({ label, value, tone = 'neutral' }: { label: string; value: React.ReactNode; tone?: 'neutral' | 'good' | 'bad' }) {
  const toneClass = tone === 'good' ? 'text-emerald-200' : tone === 'bad' ? 'text-red-200' : 'text-white'
  return (
    <div className="rounded border border-white/10 bg-white/[0.03] p-3">
      <div className="text-xs uppercase tracking-normal text-white/45">{label}</div>
      <div className={`mt-1 text-xl font-semibold ${toneClass}`}>{value}</div>
    </div>
  )
}

export function BudgetUsageMetric({ label, trend }: { label: string; trend?: PriceBudgetTrendStats }) {
  const used = trend ? Number(trend.percent_used) : 0
  const width = `${Math.max(0, Math.min(100, used * 100))}%`
  return (
    <div className="rounded border border-white/10 bg-white/[0.03] p-3">
      <div className="text-xs uppercase tracking-normal text-white/45">{label}</div>
      {trend ? (
        <>
          <div className="mt-2 flex flex-wrap items-baseline justify-between gap-2">
            <div className="text-lg font-semibold text-cyan-100">{money(trend.total_cost, trend.currency)}</div>
            <div className="text-sm text-white/60">{percent(trend.percent_used)}%</div>
          </div>
          <div className="mt-2 h-2 overflow-hidden rounded bg-white/10">
            <div className="h-full rounded bg-cyan-400" style={{ width }} />
          </div>
          <div className="mt-2 text-xs text-white/45">
            Remaining {trend.remaining ? money(trend.remaining, trend.currency) : '-'} of {trend.limit ? money(trend.limit, trend.currency) : '-'}
          </div>
        </>
      ) : (
        <div className="mt-2 text-sm text-white/45">No current monthly budget usage</div>
      )}
    </div>
  )
}

export function PermissionCheckbox({
  label,
  checked,
  onChange,
}: {
  label: string
  checked: boolean
  onChange: (checked: boolean) => void
}) {
  return (
    <label className="inline-flex min-h-8 items-center gap-2 rounded border border-white/10 bg-white/[0.03] px-2 text-xs text-white/75">
      <input className="h-4 w-4 accent-cyan-400" type="checkbox" checked={checked} onChange={event => onChange(event.target.checked)} />
      {label}
    </label>
  )
}

export function policyPayload(base: {
  scope: 'gateway_credential' | 'gateway_credential_vault'
  credentialId: number
  vaultId?: number | null
  monthly: string
  mode: PriceBudgetMode
  currency: string
}): PriceBudgetPolicyPayload {
  return {
    scope: base.scope,
    gateway_credential_id: base.credentialId,
    vault_id: base.vaultId ?? null,
    provider_key: null,
    mode: base.mode,
    currency: base.currency || 'USD',
    max_run_cost: null,
    max_daily_cost: null,
    max_monthly_cost: base.monthly.trim() || null,
    require_verified_catalog: true,
    allow_stale_catalog: false,
    max_catalog_age_seconds: 43200,
  }
}
