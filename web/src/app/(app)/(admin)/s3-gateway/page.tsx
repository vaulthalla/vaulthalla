'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { AdminPage } from '@/components/admin/AdminPage'
import RefreshIcon from '@/fa-duotone/arrows-rotate.svg'
import CheckIcon from '@/fa-duotone/circle-check.svg'
import XIcon from '@/fa-duotone/circle-xmark.svg'
import CopyIcon from '@/fa-duotone/copy.svg'
import EyeIcon from '@/fa-duotone/eye.svg'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import KeyIcon from '@/fa-duotone/key.svg'
import PlusIcon from '@/fa-duotone/plus.svg'
import ShieldIcon from '@/fa-duotone/shield-check.svg'
import TerminalIcon from '@/fa-duotone/terminal.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import VaultIcon from '@/fa-duotone/vault.svg'
import { PriceBudgetMode, PriceBudgetPolicyPayload, PriceBudgetTrendStats } from '@/models/pricing/priceBudget'
import {
  S3GatewayCredentialScopeMode,
  S3GatewayCredentialVaultScopePayload,
} from '@/models/s3Gateway'
import { useS3GatewayStore } from '@/stores/s3GatewayStore'
import { useVaultStore } from '@/stores/vaultStore'

const cardClass = 'rounded border border-white/10 bg-zinc-950/65 text-white shadow-xl'
const fieldClass = 'min-h-10 rounded border border-white/10 bg-zinc-950 px-3 py-2 text-sm text-white outline-none focus:border-cyan-400'
const buttonClass = 'inline-flex min-h-10 items-center justify-center gap-2 rounded border border-white/10 bg-white/[0.06] px-3 py-2 text-sm text-white hover:bg-white/[0.1] disabled:cursor-not-allowed disabled:opacity-50'
const primaryButtonClass = 'inline-flex min-h-10 items-center justify-center gap-2 rounded bg-cyan-500 px-3 py-2 text-sm font-medium text-zinc-950 hover:bg-cyan-300 disabled:cursor-not-allowed disabled:opacity-50'
const dangerButtonClass = 'inline-flex min-h-10 items-center justify-center gap-2 rounded border border-red-300/25 bg-red-500/10 px-3 py-2 text-sm text-red-100 hover:bg-red-500/20 disabled:cursor-not-allowed disabled:opacity-50'
const modes: PriceBudgetMode[] = ['report', 'warn', 'enforce']
const scopeModes: S3GatewayCredentialScopeMode[] = ['user_access', 'vault_allowlist', 'global']

const formatDate = (value: number | string | null | undefined) => {
  if (value == null) return '-'
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? String(value) : date.toLocaleString()
}

const money = (value: string | number | null | undefined, currency = 'USD') => {
  const amount = Number(value ?? 0)
  if (!Number.isFinite(amount)) return `${value ?? '0'} ${currency}`
  return new Intl.NumberFormat(undefined, { style: 'currency', currency, maximumFractionDigits: 4 }).format(amount)
}

const percent = (value: number) => new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 }).format(value * 100)

const scopeLabel = (scope: S3GatewayCredentialScopeMode) => scope.replace('_', '-')

function Section({ title, icon: Icon, right, children }: {
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

function Metric({ label, value, tone = 'neutral' }: { label: string; value: React.ReactNode; tone?: 'neutral' | 'good' | 'bad' }) {
  const toneClass = tone === 'good' ? 'text-emerald-200' : tone === 'bad' ? 'text-red-200' : 'text-white'
  return (
    <div className="rounded border border-white/10 bg-white/[0.03] p-3">
      <div className="text-xs uppercase tracking-normal text-white/45">{label}</div>
      <div className={`mt-1 text-xl font-semibold ${toneClass}`}>{value}</div>
    </div>
  )
}

function BudgetUsageMetric({ label, trend }: { label: string; trend?: PriceBudgetTrendStats }) {
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

function PermissionCheckbox({
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

function policyPayload(base: {
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

export default function S3GatewayPage() {
  const {
    status,
    credentials,
    scopesByCredentialId,
    buckets,
    policies,
    ledger,
    budgetStatus,
    createdSecret,
    loading,
    saving,
    error,
    fetchStatus,
    fetchCredentials,
    createCredential,
    revokeCredential,
    updateCredentialScope,
    fetchCredentialScopes,
    fetchBuckets,
    bindBucket,
    unbindBucket,
    createLocalBucket,
    createRemoteCacheBucket,
    fetchPolicies,
    upsertPolicy,
    disablePolicy,
    fetchLedger,
    fetchBudgetStatus,
    clearCreatedSecret,
  } = useS3GatewayStore()
  const { vaults, fetchVaults } = useVaultStore()

  const [selectedCredentialId, setSelectedCredentialId] = useState<number | null>(null)
  const selectedCredential = useMemo(
    () => credentials.find(item => item.id === selectedCredentialId) ?? credentials[0] ?? null,
    [credentials, selectedCredentialId],
  )
  const selectedScopes = useMemo(
    () => (selectedCredential ? scopesByCredentialId[selectedCredential.id] ?? [] : []),
    [scopesByCredentialId, selectedCredential],
  )
  const vaultById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault])), [vaults])
  const endpoint = status?.endpoint || '127.0.0.1:9000'

  const [createOpen, setCreateOpen] = useState(false)
  const [newName, setNewName] = useState('')
  const [newPrincipal, setNewPrincipal] = useState('')
  const [newScopeMode, setNewScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [newDescription, setNewDescription] = useState('')
  const [newExpiresDays, setNewExpiresDays] = useState('')
  const [newVaultIds, setNewVaultIds] = useState<number[]>([])
  const [newPerms, setNewPerms] = useState({ can_list: true, can_read: true, can_write: false, can_delete: false, can_admin: false })

  const [editScopeMode, setEditScopeMode] = useState<S3GatewayCredentialScopeMode>('user_access')
  const [editDescription, setEditDescription] = useState('')
  const [scopeDraft, setScopeDraft] = useState<S3GatewayCredentialVaultScopePayload[]>([])

  const [bindBucketName, setBindBucketName] = useState('')
  const [bindVaultId, setBindVaultId] = useState('')
  const [bindMode, setBindMode] = useState('local')
  const bindVault = useMemo(() => vaults.find(vault => String(vault.id) === bindVaultId) ?? null, [bindVaultId, vaults])
  const bindModeOptions = useMemo(() => (bindVault?.type === 's3' ? ['remote_cache', 'remote_proxy'] : ['local']), [bindVault])
  const [localBucketName, setLocalBucketName] = useState('')
  const [remoteBucketName, setRemoteBucketName] = useState('')
  const [remoteApiKey, setRemoteApiKey] = useState('')
  const [remoteUpstreamBucket, setRemoteUpstreamBucket] = useState('')

  const [budgetMode, setBudgetMode] = useState<PriceBudgetMode>('enforce')
  const [budgetCurrency, setBudgetCurrency] = useState('USD')
  const [keyMonthly, setKeyMonthly] = useState('')
  const [keyVaultMonthly, setKeyVaultMonthly] = useState('')
  const [budgetVaultId, setBudgetVaultId] = useState('')

  useEffect(() => {
    void Promise.all([
      fetchStatus(),
      fetchCredentials(),
      fetchBuckets(),
      fetchPolicies({ include_inactive: true }),
      fetchLedger({ limit: 25 }),
      fetchBudgetStatus({ limit: 25 }),
      fetchVaults(),
    ]).catch(() => undefined)
  }, [fetchBuckets, fetchBudgetStatus, fetchCredentials, fetchLedger, fetchPolicies, fetchStatus, fetchVaults])

  useEffect(() => {
    if (!selectedCredential) return
    setEditScopeMode(selectedCredential.scope_mode)
    setEditDescription(selectedCredential.description ?? '')
    void fetchCredentialScopes({ access_key: selectedCredential.access_key }).catch(() => undefined)
    void fetchPolicies({ gateway_credential_id: selectedCredential.id, include_inactive: true }).catch(() => undefined)
    void fetchLedger({ gateway_credential_id: selectedCredential.id, limit: 25 }).catch(() => undefined)
    void fetchBudgetStatus({ gateway_credential_id: selectedCredential.id, limit: 25 }).catch(() => undefined)
  }, [selectedCredential, fetchBudgetStatus, fetchCredentialScopes, fetchLedger, fetchPolicies])

  useEffect(() => {
    setScopeDraft(selectedScopes.map(scope => ({
      vault_id: scope.vault_id,
      can_list: scope.can_list,
      can_read: scope.can_read,
      can_write: scope.can_write,
      can_delete: scope.can_delete,
      can_admin: scope.can_admin,
    })))
  }, [selectedScopes])

  useEffect(() => {
    if (!bindModeOptions.includes(bindMode)) setBindMode(bindModeOptions[0])
  }, [bindMode, bindModeOptions])

  const refreshAll = async () => {
    await Promise.all([
      fetchStatus(),
      fetchCredentials(),
      fetchBuckets(),
      fetchPolicies(selectedCredential ? { gateway_credential_id: selectedCredential.id, include_inactive: true } : { include_inactive: true }),
      fetchLedger(selectedCredential ? { gateway_credential_id: selectedCredential.id, limit: 25 } : { limit: 25 }),
      fetchBudgetStatus(selectedCredential ? { gateway_credential_id: selectedCredential.id, limit: 25 } : { limit: 25 }),
    ])
  }

  const buildNewVaultScopes = () => (
    newScopeMode === 'vault_allowlist'
      ? newVaultIds.map(vault_id => ({ vault_id, ...newPerms }))
      : []
  )

  const submitCreate = async () => {
    const expires = Number(newExpiresDays)
    const expires_at = Number.isFinite(expires) && expires > 0
      ? Math.floor(Date.now() / 1000) + Math.floor(expires * 86400)
      : null
    const principal = Number(newPrincipal)
    const result = await createCredential({
      name: newName,
      principal_user_id: Number.isFinite(principal) && principal > 0 ? principal : null,
      scope_mode: newScopeMode,
      description: newDescription.trim() || null,
      expires_at,
      vault_scopes: buildNewVaultScopes(),
    })
    setSelectedCredentialId(result.credential.id)
    setCreateOpen(false)
    setNewName('')
    setNewPrincipal('')
    setNewDescription('')
    setNewExpiresDays('')
    setNewVaultIds([])
  }

  const updateScopeDraft = (vaultId: number, key: keyof Omit<S3GatewayCredentialVaultScopePayload, 'vault_id'>, value: boolean) => {
    setScopeDraft(rows => rows.map(row => (row.vault_id === vaultId ? { ...row, [key]: value } : row)))
  }

  const addScopeVault = (vaultId: number) => {
    if (scopeDraft.some(row => row.vault_id === vaultId)) return
    setScopeDraft(rows => [...rows, { vault_id: vaultId, can_list: true, can_read: true, can_write: false, can_delete: false, can_admin: false }])
  }

  const saveScope = async () => {
    if (!selectedCredential) return
    await updateCredentialScope({
      access_key: selectedCredential.access_key,
      scope_mode: editScopeMode,
      description: editDescription.trim() || null,
      vault_scopes: scopeDraft,
    })
  }

  const selectedPolicies = policies.filter(policy => selectedCredential && policy.gateway_credential_id === selectedCredential.id)
  const keyPolicy = selectedPolicies.find(policy => policy.scope === 'gateway_credential' && policy.is_active)
  const keyVaultPolicy = selectedPolicies.find(policy => (
    policy.scope === 'gateway_credential_vault'
    && policy.is_active
    && (!budgetVaultId || policy.vault_id === Number(budgetVaultId))
  ))
  const monthlyTrends = (budgetStatus?.trends ?? []).filter(trend => trend.window_type === 'monthly')
  const keyUsageTrend = monthlyTrends.find(trend => (
    selectedCredential
    && trend.scope === 'gateway_credential'
    && trend.gateway_credential_id === selectedCredential.id
  ))
  const keyVaultUsageTrend = monthlyTrends.find(trend => (
    selectedCredential
    && trend.scope === 'gateway_credential_vault'
    && trend.gateway_credential_id === selectedCredential.id
    && (!budgetVaultId || trend.vault_id === Number(budgetVaultId))
  ))

  const saveKeyBudget = async () => {
    if (!selectedCredential) return
    await upsertPolicy(policyPayload({
      scope: 'gateway_credential',
      credentialId: selectedCredential.id,
      monthly: keyMonthly,
      mode: budgetMode,
      currency: budgetCurrency,
    }))
  }

  const saveKeyVaultBudget = async () => {
    if (!selectedCredential || !budgetVaultId) return
    await upsertPolicy(policyPayload({
      scope: 'gateway_credential_vault',
      credentialId: selectedCredential.id,
      vaultId: Number(budgetVaultId),
      monthly: keyVaultMonthly,
      mode: budgetMode,
      currency: budgetCurrency,
    }))
  }

  const copy = (text: string) => {
    void navigator.clipboard?.writeText(text)
  }

  const snippetAccessKey = createdSecret?.credential.access_key || selectedCredential?.access_key || 'VH_ACCESS_KEY'
  const snippetSecret = createdSecret?.secret_access_key || 'VH_SECRET_ACCESS_KEY'

  return (
    <AdminPage title="S3 Gateway" description="Scoped S3-compatible access, bucket bindings, and gateway cost controls.">
      <div className="space-y-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div className="text-sm text-white/55">{error || (loading ? 'Loading gateway state' : '')}</div>
          <button className={buttonClass} type="button" disabled={loading} onClick={() => void refreshAll()}>
            <RefreshIcon className="h-4 w-4" />
            Refresh
          </button>
        </div>

        <Section title="Service" icon={ShieldIcon}>
          <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-6">
            <Metric label="Running" value={status?.running ? 'Yes' : 'No'} tone={status?.running ? 'good' : 'bad'} />
            <Metric label="Configured" value={status?.configured ? 'Yes' : 'No'} tone={status?.configured ? 'good' : 'bad'} />
            <Metric label="Ready" value={status?.ready ? 'Yes' : 'No'} tone={status?.ready ? 'good' : 'bad'} />
            <Metric label="Endpoint" value={endpoint} />
            <Metric label="Sessions" value={status?.active_sessions ?? 0} />
            <Metric label="Requests" value={`${status?.total_requests ?? 0} / ${status?.failed_requests ?? 0}`} />
          </div>
        </Section>

        <Section
          title="Credentials"
          icon={KeyIcon}
          right={
            <button className={primaryButtonClass} type="button" onClick={() => setCreateOpen(true)}>
              <PlusIcon className="h-4 w-4" />
              Create
            </button>
          }>
          {createdSecret && (
            <div className="mb-4 rounded border border-emerald-300/25 bg-emerald-500/10 p-3 text-sm text-emerald-50">
              <div className="mb-2 flex items-center gap-2 font-medium">
                <EyeIcon className="h-4 w-4" />
                Secret access key
              </div>
              <div className="grid gap-2 md:grid-cols-[1fr_auto]">
                <code className="overflow-x-auto rounded bg-black/35 p-2 text-xs">{createdSecret.secret_access_key}</code>
                <button className={buttonClass} type="button" onClick={() => copy(createdSecret.secret_access_key)}>
                  <CopyIcon className="h-4 w-4" />
                  Copy
                </button>
              </div>
              <button className="mt-2 text-xs text-emerald-100/75 underline" type="button" onClick={clearCreatedSecret}>
                Hide
              </button>
            </div>
          )}

          <div className="overflow-x-auto">
            <table className="min-w-full text-left text-sm">
              <thead className="text-xs uppercase tracking-normal text-white/45">
                <tr>
                  <th className="px-3 py-2">Name</th>
                  <th className="px-3 py-2">Access key</th>
                  <th className="px-3 py-2">Principal</th>
                  <th className="px-3 py-2">Scope</th>
                  <th className="px-3 py-2">Enabled</th>
                  <th className="px-3 py-2">Expires</th>
                  <th className="px-3 py-2">Last used</th>
                  <th className="px-3 py-2"></th>
                </tr>
              </thead>
              <tbody className="divide-y divide-white/10">
                {credentials.map(credential => (
                  <tr key={credential.id} className={selectedCredential?.id === credential.id ? 'bg-cyan-400/10' : ''}>
                    <td className="px-3 py-2 font-medium text-white">{credential.name}</td>
                    <td className="px-3 py-2 font-mono text-xs text-white/70">{credential.access_key}</td>
                    <td className="px-3 py-2 text-white/70">{credential.principal_user_id}</td>
                    <td className="px-3 py-2 text-white/70">{scopeLabel(credential.scope_mode)}</td>
                    <td className="px-3 py-2">{credential.enabled ? <CheckIcon className="h-4 w-4 text-emerald-300" /> : <XIcon className="h-4 w-4 text-red-300" />}</td>
                    <td className="px-3 py-2 text-white/55">{formatDate(credential.expires_at)}</td>
                    <td className="px-3 py-2 text-white/55">{formatDate(credential.last_used_at)}</td>
                    <td className="px-3 py-2">
                      <div className="flex gap-2">
                        <button className={buttonClass} type="button" onClick={() => setSelectedCredentialId(credential.id)}>
                          <EyeIcon className="h-4 w-4" />
                          Select
                        </button>
                        <button className={dangerButtonClass} type="button" onClick={() => void revokeCredential({ access_key: credential.access_key })}>
                          <TrashIcon className="h-4 w-4" />
                          Revoke
                        </button>
                      </div>
                    </td>
                  </tr>
                ))}
                {credentials.length === 0 && (
                  <tr>
                    <td className="px-3 py-6 text-center text-white/50" colSpan={8}>No gateway credentials</td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </Section>

        {createOpen && (
          <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4">
            <div className="max-h-[90vh] w-full max-w-3xl overflow-y-auto rounded border border-white/10 bg-zinc-950 p-5 text-white shadow-2xl">
              <div className="mb-4 flex items-center justify-between gap-3">
                <h2 className="text-lg font-semibold text-cyan-100">Create credential</h2>
                <button className={buttonClass} type="button" onClick={() => setCreateOpen(false)}>Close</button>
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Name
                  <input className={fieldClass} value={newName} onChange={event => setNewName(event.target.value)} />
                </label>
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Principal user id
                  <input className={fieldClass} inputMode="numeric" value={newPrincipal} onChange={event => setNewPrincipal(event.target.value)} />
                </label>
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Scope
                  <select className={fieldClass} value={newScopeMode} onChange={event => setNewScopeMode(event.target.value as S3GatewayCredentialScopeMode)}>
                    {scopeModes.map(mode => <option key={mode} value={mode}>{scopeLabel(mode)}</option>)}
                  </select>
                </label>
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Expires in days
                  <input className={fieldClass} inputMode="decimal" value={newExpiresDays} onChange={event => setNewExpiresDays(event.target.value)} />
                </label>
                <label className="flex flex-col gap-1 text-xs text-white/60 md:col-span-2">
                  Description
                  <input className={fieldClass} value={newDescription} onChange={event => setNewDescription(event.target.value)} />
                </label>
              </div>

              {newScopeMode === 'vault_allowlist' && (
                <div className="mt-4 space-y-3">
                  <div className="flex flex-wrap gap-2">
                    {(['can_list', 'can_read', 'can_write', 'can_delete', 'can_admin'] as const).map(key => (
                      <PermissionCheckbox
                        key={key}
                        label={key.replace('can_', '')}
                        checked={newPerms[key]}
                        onChange={checked => setNewPerms(perms => ({ ...perms, [key]: checked }))}
                      />
                    ))}
                  </div>
                  <div className="grid gap-2 md:grid-cols-3">
                    {vaults.map(vault => (
                      <label key={vault.id} className="flex min-h-10 items-center gap-2 rounded border border-white/10 bg-white/[0.03] px-3 text-sm text-white/75">
                        <input
                          className="h-4 w-4 accent-cyan-400"
                          type="checkbox"
                          checked={newVaultIds.includes(vault.id)}
                          onChange={event => {
                            setNewVaultIds(ids => event.target.checked ? [...ids, vault.id] : ids.filter(id => id !== vault.id))
                          }}
                        />
                        {vault.name} <span className="text-white/35">#{vault.id}</span>
                      </label>
                    ))}
                  </div>
                </div>
              )}

              <div className="mt-5 flex justify-end gap-2">
                <button className={buttonClass} type="button" onClick={() => setCreateOpen(false)}>Cancel</button>
                <button className={primaryButtonClass} type="button" disabled={!newName.trim() || saving} onClick={() => void submitCreate()}>
                  <PlusIcon className="h-4 w-4" />
                  Create
                </button>
              </div>
            </div>
          </div>
        )}

        <Section title="Scope Editor" icon={ShieldIcon}>
          {selectedCredential ? (
            <div className="space-y-4">
              <div className="grid gap-3 md:grid-cols-[220px_1fr_auto]">
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Scope
                  <select className={fieldClass} value={editScopeMode} onChange={event => setEditScopeMode(event.target.value as S3GatewayCredentialScopeMode)}>
                    {scopeModes.map(mode => <option key={mode} value={mode}>{scopeLabel(mode)}</option>)}
                  </select>
                </label>
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Description
                  <input className={fieldClass} value={editDescription} onChange={event => setEditDescription(event.target.value)} />
                </label>
                <button className={`${primaryButtonClass} self-end`} type="button" disabled={saving} onClick={() => void saveScope()}>
                  <SaveIcon className="h-4 w-4" />
                  Save
                </button>
              </div>

              <div className="grid gap-3">
                {scopeDraft.map(row => (
                  <div key={row.vault_id} className="grid gap-3 rounded border border-white/10 bg-white/[0.03] p-3 lg:grid-cols-[1fr_auto]">
                    <div>
                      <div className="font-medium text-white">{vaultById.get(row.vault_id)?.name ?? `Vault ${row.vault_id}`}</div>
                      <div className="text-xs text-white/45">#{row.vault_id}</div>
                    </div>
                    <div className="flex flex-wrap gap-2">
                      {(['can_list', 'can_read', 'can_write', 'can_delete', 'can_admin'] as const).map(key => (
                        <PermissionCheckbox
                          key={key}
                          label={key.replace('can_', '')}
                          checked={!!row[key]}
                          onChange={checked => updateScopeDraft(row.vault_id, key, checked)}
                        />
                      ))}
                      <button className={dangerButtonClass} type="button" onClick={() => setScopeDraft(rows => rows.filter(item => item.vault_id !== row.vault_id))}>
                        <TrashIcon className="h-4 w-4" />
                        Remove
                      </button>
                    </div>
                  </div>
                ))}
              </div>

              <div className="flex flex-wrap gap-2">
                {vaults.filter(vault => !scopeDraft.some(row => row.vault_id === vault.id)).map(vault => (
                  <button key={vault.id} className={buttonClass} type="button" onClick={() => addScopeVault(vault.id)}>
                    <PlusIcon className="h-4 w-4" />
                    {vault.name}
                  </button>
                ))}
              </div>
            </div>
          ) : (
            <div className="py-6 text-center text-white/50">Select a credential</div>
          )}
        </Section>

        <Section title="Bucket Bindings" icon={VaultIcon}>
          <div className="grid gap-3 lg:grid-cols-3">
            <div className="rounded border border-white/10 bg-white/[0.03] p-3">
              <div className="mb-3 text-sm font-medium text-white">Bind vault</div>
              <div className="space-y-2">
                <input className={fieldClass} placeholder="Bucket name" value={bindBucketName} onChange={event => setBindBucketName(event.target.value)} />
                <select className={fieldClass} value={bindVaultId} onChange={event => setBindVaultId(event.target.value)}>
                  <option value="">Vault</option>
                  {vaults.map(vault => <option key={vault.id} value={vault.id}>{vault.name}</option>)}
                </select>
                <select className={fieldClass} value={bindMode} onChange={event => setBindMode(event.target.value)}>
                  {bindModeOptions.map(mode => <option key={mode} value={mode}>{mode}</option>)}
                </select>
                <button className={primaryButtonClass} type="button" disabled={!bindBucketName || !bindVaultId} onClick={() => void bindBucket({ bucket_name: bindBucketName, vault_id: Number(bindVaultId), mode: bindMode })}>
                  <PlusIcon className="h-4 w-4" />
                  Bind
                </button>
              </div>
            </div>

            <div className="rounded border border-white/10 bg-white/[0.03] p-3">
              <div className="mb-3 text-sm font-medium text-white">Create local</div>
              <div className="space-y-2">
                <input className={fieldClass} placeholder="Bucket name" value={localBucketName} onChange={event => setLocalBucketName(event.target.value)} />
                <button className={primaryButtonClass} type="button" disabled={!localBucketName} onClick={() => void createLocalBucket({ bucket_name: localBucketName })}>
                  <PlusIcon className="h-4 w-4" />
                  Create
                </button>
              </div>
            </div>

            <div className="rounded border border-white/10 bg-white/[0.03] p-3">
              <div className="mb-3 text-sm font-medium text-white">Create remote-cache</div>
              <div className="space-y-2">
                <input className={fieldClass} placeholder="Bucket name" value={remoteBucketName} onChange={event => setRemoteBucketName(event.target.value)} />
                <input className={fieldClass} placeholder="API key id or name" value={remoteApiKey} onChange={event => setRemoteApiKey(event.target.value)} />
                <input className={fieldClass} placeholder="Upstream bucket" value={remoteUpstreamBucket} onChange={event => setRemoteUpstreamBucket(event.target.value)} />
                <button
                  className={primaryButtonClass}
                  type="button"
                  disabled={!remoteBucketName || !remoteApiKey || !remoteUpstreamBucket}
                  onClick={() => void createRemoteCacheBucket({
                    bucket_name: remoteBucketName,
                    api_key: remoteApiKey,
                    upstream_bucket: remoteUpstreamBucket,
                    encrypt_upstream: true,
                  })}>
                  <PlusIcon className="h-4 w-4" />
                  Create
                </button>
              </div>
            </div>
          </div>

          <div className="mt-4 overflow-x-auto">
            <table className="min-w-full text-left text-sm">
              <thead className="text-xs uppercase tracking-normal text-white/45">
                <tr>
                  <th className="px-3 py-2">Bucket</th>
                  <th className="px-3 py-2">Vault</th>
                  <th className="px-3 py-2">Mode</th>
                  <th className="px-3 py-2">API exclusive</th>
                  <th className="px-3 py-2"></th>
                </tr>
              </thead>
              <tbody className="divide-y divide-white/10">
                {buckets.map(bucket => (
                  <tr key={bucket.bucket_name}>
                    <td className="px-3 py-2 font-medium text-white">{bucket.bucket_name}</td>
                    <td className="px-3 py-2 text-white/70">{vaultById.get(bucket.vault_id)?.name ?? bucket.vault_id}</td>
                    <td className="px-3 py-2 text-white/70">{bucket.mode}</td>
                    <td className="px-3 py-2">{bucket.api_exclusive ? <CheckIcon className="h-4 w-4 text-emerald-300" /> : <XIcon className="h-4 w-4 text-white/35" />}</td>
                    <td className="px-3 py-2">
                      <button className={dangerButtonClass} type="button" onClick={() => void unbindBucket({ bucket_name: bucket.bucket_name })}>
                        <TrashIcon className="h-4 w-4" />
                        Unbind
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Section>

        <Section title="Budgets" icon={ShieldIcon}>
          {selectedCredential ? (
            <div className="space-y-4">
              <div className="grid gap-3 md:grid-cols-4">
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Mode
                  <select className={fieldClass} value={budgetMode} onChange={event => setBudgetMode(event.target.value as PriceBudgetMode)}>
                    {modes.map(mode => <option key={mode} value={mode}>{mode}</option>)}
                  </select>
                </label>
                <label className="flex flex-col gap-1 text-xs text-white/60">
                  Currency
                  <input className={fieldClass} value={budgetCurrency} onChange={event => setBudgetCurrency(event.target.value.toUpperCase())} />
                </label>
                <Metric label="Key cap" value={keyPolicy ? money(keyPolicy.max_monthly_cost, keyPolicy.currency) : '-'} />
                <Metric label="Key/vault cap" value={keyVaultPolicy ? money(keyVaultPolicy.max_monthly_cost, keyVaultPolicy.currency) : '-'} />
              </div>
              <div className="grid gap-3 md:grid-cols-2">
                <BudgetUsageMetric label="Key month usage" trend={keyUsageTrend} />
                <BudgetUsageMetric label="Key/vault month usage" trend={keyVaultUsageTrend} />
              </div>

              <div className="grid gap-3 lg:grid-cols-2">
                <div className="rounded border border-white/10 bg-white/[0.03] p-3">
                  <div className="mb-3 text-sm font-medium text-white">Per-key monthly cap</div>
                  <div className="flex flex-wrap gap-2">
                    <input className={fieldClass} inputMode="decimal" placeholder="Amount" value={keyMonthly} onChange={event => setKeyMonthly(event.target.value)} />
                    <button className={primaryButtonClass} type="button" disabled={!keyMonthly || saving} onClick={() => void saveKeyBudget()}>
                      <SaveIcon className="h-4 w-4" />
                      Save
                    </button>
                    <button
                      className={dangerButtonClass}
                      type="button"
                      disabled={!keyPolicy}
                      onClick={() => void disablePolicy(policyPayload({ scope: 'gateway_credential', credentialId: selectedCredential.id, monthly: keyMonthly, mode: budgetMode, currency: budgetCurrency }))}>
                      <TrashIcon className="h-4 w-4" />
                      Disable
                    </button>
                  </div>
                </div>

                <div className="rounded border border-white/10 bg-white/[0.03] p-3">
                  <div className="mb-3 text-sm font-medium text-white">Per-key/vault monthly cap</div>
                  <div className="flex flex-wrap gap-2">
                    <select className={fieldClass} value={budgetVaultId} onChange={event => setBudgetVaultId(event.target.value)}>
                      <option value="">Vault</option>
                      {vaults.map(vault => <option key={vault.id} value={vault.id}>{vault.name}</option>)}
                    </select>
                    <input className={fieldClass} inputMode="decimal" placeholder="Amount" value={keyVaultMonthly} onChange={event => setKeyVaultMonthly(event.target.value)} />
                    <button className={primaryButtonClass} type="button" disabled={!budgetVaultId || !keyVaultMonthly || saving} onClick={() => void saveKeyVaultBudget()}>
                      <SaveIcon className="h-4 w-4" />
                      Save
                    </button>
                    <button
                      className={dangerButtonClass}
                      type="button"
                      disabled={!keyVaultPolicy || !budgetVaultId}
                      onClick={() => void disablePolicy(policyPayload({
                        scope: 'gateway_credential_vault',
                        credentialId: selectedCredential.id,
                        vaultId: Number(budgetVaultId),
                        monthly: keyVaultMonthly,
                        mode: budgetMode,
                        currency: budgetCurrency,
                      }))}>
                      <TrashIcon className="h-4 w-4" />
                      Disable
                    </button>
                  </div>
                </div>
              </div>

              <div className="grid gap-3 md:grid-cols-3">
                {selectedPolicies.filter(policy => policy.is_active).map(policy => (
                  <div key={policy.id ?? `${policy.scope}-${policy.vault_id ?? 0}`} className="rounded border border-white/10 bg-white/[0.03] p-3">
                    <div className="text-sm font-medium text-white">{policy.scope}</div>
                    <div className="mt-1 text-xs text-white/45">
                      {policy.vault_id ? `Vault ${vaultById.get(policy.vault_id)?.name ?? policy.vault_id}` : 'All gateway vaults'}
                    </div>
                    <div className="mt-3 text-lg font-semibold text-cyan-100">{money(policy.max_monthly_cost, policy.currency)}</div>
                    <div className="text-xs text-white/45">{policy.mode}</div>
                  </div>
                ))}
              </div>

              <div className="overflow-x-auto rounded border border-white/10">
                <table className="min-w-full text-left text-sm">
                  <thead className="bg-white/[0.03] text-xs uppercase tracking-normal text-white/45">
                    <tr>
                      <th className="px-3 py-2">Operation</th>
                      <th className="px-3 py-2">Vault</th>
                      <th className="px-3 py-2">Cost</th>
                      <th className="px-3 py-2">Status</th>
                      <th className="px-3 py-2">Object</th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-white/10">
                    {ledger.map(row => (
                      <tr key={row.id ?? `${row.request_uuid}-${row.created_at}`}>
                        <td className="px-3 py-2 text-white">{row.operation ?? '-'}</td>
                        <td className="px-3 py-2 text-white/70">{vaultById.get(row.vault_id)?.name ?? row.vault_id}</td>
                        <td className="px-3 py-2 text-white/70">{money(row.committed_cost ?? row.reserved_cost, row.currency)}</td>
                        <td className="px-3 py-2 text-white/70">{row.status}</td>
                        <td className="max-w-[240px] truncate px-3 py-2 text-white/55">{row.object_key ?? '-'}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          ) : (
            <div className="py-6 text-center text-white/50">Select a credential</div>
          )}
        </Section>

        <Section title="Client Setup" icon={TerminalIcon}>
          <div className="grid gap-3 lg:grid-cols-2">
            {[
              {
                title: 'Environment',
                text: `export AWS_ACCESS_KEY_ID=${snippetAccessKey}\nexport AWS_SECRET_ACCESS_KEY=${snippetSecret}\nexport AWS_EC2_METADATA_DISABLED=true`,
              },
              {
                title: 'AWS CLI',
                text: `aws --endpoint-url http://${endpoint} s3 ls\naws --endpoint-url http://${endpoint} s3 cp ./backup.tar s3://${buckets[0]?.bucket_name ?? 'bucket'}/backup.tar`,
              },
              {
                title: 'MinIO Client',
                text: `mc alias set vaulthalla http://${endpoint} ${snippetAccessKey} ${snippetSecret}\nmc ls vaulthalla/${buckets[0]?.bucket_name ?? 'bucket'}`,
              },
            ].map(snippet => (
              <div key={snippet.title} className="rounded border border-white/10 bg-black/25 p-3">
                <div className="mb-2 flex items-center justify-between gap-2">
                  <div className="text-sm font-medium text-white">{snippet.title}</div>
                  <button className={buttonClass} type="button" onClick={() => copy(snippet.text)}>
                    <CopyIcon className="h-4 w-4" />
                    Copy
                  </button>
                </div>
                <pre className="overflow-x-auto whitespace-pre-wrap rounded bg-black/35 p-3 text-xs text-cyan-50">{snippet.text}</pre>
              </div>
            ))}
          </div>
        </Section>
      </div>
    </AdminPage>
  )
}
