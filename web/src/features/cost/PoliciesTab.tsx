'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { api } from '@/lib/session'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { Button } from '@/components/ui/Button'
import { EmptyState } from '@/components/ui/State'
import { PenIcon } from '@/components/ui/icons'
import { formatMoney } from '@/lib/format'
import { BUDGET_PROVIDERS, type BudgetPolicy, type PolicyPayload } from '@/features/cost/model'
import { findPolicy, refreshPricing, type VaultLite } from '@/features/cost/queries'
import { PolicyDialog, type PolicyTarget } from '@/features/cost/PolicyDialog'
import { LimitsSummary, ModeBadge, Section } from '@/features/cost/bits'

interface Row {
  key: string
  target: PolicyTarget
  label: React.ReactNode
  detail?: React.ReactNode
  policy: BudgetPolicy | null
}

const savePolicy = async (payload: PolicyPayload) => {
  await api.send('pricing.budget.policy.upsert', payload)
  await refreshPricing()
}

const disablePolicy = async (target: PolicyTarget) => {
  await api.send('pricing.budget.policy.disable', {
    scope: target.scope,
    provider_key: target.provider_key ?? null,
    vault_id: target.vault_id ?? null,
  })
  await refreshPricing()
}

const PolicyRows = ({ rows, onEdit }: { rows: Row[]; onEdit: (row: Row) => void }) => {
  const columns: Column<Row>[] = [
    {
      key: 'target',
      header: 'Applies to',
      cell: row => (
        <div className="min-w-0 py-2">
          <div className="text-fg font-medium">{row.label}</div>
          {row.detail ?
            <div className="text-fg-subtle text-xs">{row.detail}</div>
          : null}
        </div>
      ),
    },
    { key: 'mode', header: 'Mode', headerClassName: 'w-32', cell: row => <ModeBadge policy={row.policy} /> },
    {
      key: 'limits',
      header: 'Limits',
      hideBelow: 'md',
      headerClassName: 'w-[38%]',
      cell: row => <LimitsSummary policy={row.policy} />,
    },
  ]
  return (
    <DataTable
      rows={rows}
      columns={columns}
      rowKey={row => row.key}
      onRowClick={onEdit}
      rowActions={row => (
        <Button
          variant="ghost"
          size="sm"
          onClick={() => onEdit(row)}
          aria-label={`Edit budget for ${row.target.title}`}>
          <PenIcon aria-hidden />
          <span className="hidden sm:inline">Edit</span>
        </Button>
      )}
    />
  )
}

export const PoliciesTab = ({
  policies,
  s3Vaults,
  vaultsPending,
  focusVaultId,
}: {
  policies: BudgetPolicy[]
  s3Vaults: VaultLite[]
  vaultsPending: boolean
  focusVaultId: number | null
}) => {
  const [editing, setEditing] = useState<Row | null>(null)

  const systemRows: Row[] = useMemo(
    () => [
      {
        key: 'global',
        target: { scope: 'global', title: 'All S3 storage' },
        label: 'All S3 storage',
        detail: 'Combined spend across every provider and vault',
        policy: findPolicy(policies, { scope: 'global' }),
      },
      ...BUDGET_PROVIDERS.map(p => ({
        key: `provider-${p.key}`,
        target: { scope: 'provider' as const, title: p.label, provider_key: p.key },
        label: p.label,
        detail: 'Every vault on this provider',
        policy: findPolicy(policies, { scope: 'provider', provider_key: p.key }),
      })),
    ],
    [policies],
  )

  const vaultRows: Row[] = useMemo(
    () =>
      s3Vaults.map(v => ({
        key: `vault-${v.id}`,
        target: { scope: 'vault', title: v.name, vault_id: v.id },
        label: v.name,
        detail: `Vault #${v.id}`,
        policy: findPolicy(policies, { scope: 'vault', vault_id: v.id }),
      })),
    [policies, s3Vaults],
  )

  // ?vault=<id> deep links straight into that vault's budget.
  const [focused, setFocused] = useState(false)
  useEffect(() => {
    if (focused || !focusVaultId || vaultsPending) return
    const row = vaultRows.find(r => r.target.vault_id === focusVaultId)
    if (row) setEditing(row)
    setFocused(true)
  }, [focused, focusVaultId, vaultRows, vaultsPending])

  const inherited = systemRows.filter(r => r.policy?.is_active && r.policy.mode !== 'off')

  return (
    <div className="space-y-8">
      <Section
        id="system-budgets"
        title="System budgets"
        description="Clamp combined S3 spend and spend per provider. Every sync attempt is checked against all matching budgets.">
        <PolicyRows rows={systemRows} onEdit={setEditing} />
      </Section>

      <Section
        id="vault-budgets"
        title="Vault budgets"
        description={
          inherited.length ?
            `Each vault is also clamped by ${inherited.map(r => `${r.target.title} (${r.policy?.max_monthly_cost ? `${formatMoney(r.policy.max_monthly_cost, r.policy.currency)}/mo` : r.policy?.mode})`).join(', ')}.`
          : 'Only S3 vaults incur provider costs. No system budget is active, so only a vault’s own budget applies.'
        }>
        {!vaultsPending && vaultRows.length === 0 ?
          <EmptyState
            className="panel"
            title="No S3 vaults"
            description="Budgets apply to vaults stored on S3-compatible providers."
          />
        : <PolicyRows rows={vaultRows} onEdit={setEditing} />}
      </Section>

      <PolicyDialog
        target={editing?.target ?? null}
        policy={editing?.policy ?? null}
        onClose={() => setEditing(null)}
        onSave={savePolicy}
        onDisable={disablePolicy}
      />
    </div>
  )
}
