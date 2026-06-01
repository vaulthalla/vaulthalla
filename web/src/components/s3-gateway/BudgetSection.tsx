'use client'

import { useMemo, useState } from 'react'
import SaveIcon from '@/fa-duotone/floppy-disk.svg'
import ShieldIcon from '@/fa-duotone/shield-check.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import { PriceBudgetMode, PriceBudgetPolicy, PriceBudgetPolicyPayload, PriceBudgetStatus } from '@/models/pricing/priceBudget'
import { PriceBudgetLedgerEntry } from '@/models/pricing/priceBudgetLedger'
import { S3GatewayCredential } from '@/models/s3Gateway'
import type { Vault } from '@/models/vaults'
import { LedgerSection } from './LedgerSection'
import {
  BudgetUsageMetric,
  dangerButtonClass,
  fieldClass,
  Metric,
  modes,
  money,
  policyPayload,
  primaryButtonClass,
  Section,
} from './shared'

export function BudgetSection({
  selectedCredential,
  policies,
  ledger,
  budgetStatus,
  vaults,
  saving,
  onUpsertPolicy,
  onDisablePolicy,
}: {
  selectedCredential: S3GatewayCredential | null
  policies: PriceBudgetPolicy[]
  ledger: PriceBudgetLedgerEntry[]
  budgetStatus: PriceBudgetStatus | null
  vaults: Vault[]
  saving: boolean
  onUpsertPolicy: (payload: PriceBudgetPolicyPayload) => Promise<PriceBudgetPolicy>
  onDisablePolicy: (payload: PriceBudgetPolicyPayload) => Promise<boolean>
}) {
  const vaultById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault])), [vaults])
  const [budgetMode, setBudgetMode] = useState<PriceBudgetMode>('enforce')
  const [budgetCurrency, setBudgetCurrency] = useState('USD')
  const [keyMonthly, setKeyMonthly] = useState('')
  const [keyVaultMonthly, setKeyVaultMonthly] = useState('')
  const [budgetVaultId, setBudgetVaultId] = useState('')

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
    await onUpsertPolicy(policyPayload({
      scope: 'gateway_credential',
      credentialId: selectedCredential.id,
      monthly: keyMonthly,
      mode: budgetMode,
      currency: budgetCurrency,
    }))
  }

  const saveKeyVaultBudget = async () => {
    if (!selectedCredential || !budgetVaultId) return
    await onUpsertPolicy(policyPayload({
      scope: 'gateway_credential_vault',
      credentialId: selectedCredential.id,
      vaultId: Number(budgetVaultId),
      monthly: keyVaultMonthly,
      mode: budgetMode,
      currency: budgetCurrency,
    }))
  }

  return (
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
                  onClick={() => void onDisablePolicy(policyPayload({ scope: 'gateway_credential', credentialId: selectedCredential.id, monthly: keyMonthly, mode: budgetMode, currency: budgetCurrency }))}>
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
                  onClick={() => void onDisablePolicy(policyPayload({
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

          <LedgerSection ledger={ledger} vaultById={vaultById} />
        </div>
      ) : (
        <div className="py-6 text-center text-white/50">Select a credential</div>
      )}
    </Section>
  )
}
