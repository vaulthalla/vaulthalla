'use client'

import { useEffect, useState } from 'react'
import SackDollarIcon from '@/fa-duotone/sack-dollar.svg'
import { StatsCard } from '@/components/stats/StatsCard'
import { PricingBudgetStats } from '@/models/stats/pricingBudgetStats'
import { useStatsStore } from '@/stores/statsStore'

const money = (value: string | number | null | undefined, currency = 'USD') => {
  const amount = Number(value ?? 0)
  if (!Number.isFinite(amount)) return `${value ?? '0'} ${currency}`
  return new Intl.NumberFormat(undefined, { style: 'currency', currency, maximumFractionDigits: 4 }).format(amount)
}

const Stat = ({ label, value, tone = 'text-white' }: { label: string; value: string | number; tone?: string }) => (
  <div className="rounded border border-white/10 bg-white/[0.03] p-3 text-left">
    <div className="text-xs uppercase tracking-normal text-white/45">{label}</div>
    <div className={`mt-1 text-lg font-semibold ${tone}`}>{value}</div>
  </div>
)

export default function PricingBudget({ vaultId, intervalMs = 15000 }: { vaultId: number; intervalMs?: number }) {
  const [stats, setStats] = useState<PricingBudgetStats | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    let alive = true
    const refresh = async () => {
      try {
        const next = await useStatsStore.getState().getVaultPricingStats({ vault_id: vaultId })
        if (alive) {
          setStats(next)
          setError(null)
        }
      } catch (err) {
        if (alive) setError(err instanceof Error ? err.message : 'Unable to load pricing budget stats')
      }
    }
    void refresh()
    const timer = window.setInterval(() => void refresh(), intervalMs)
    return () => {
      alive = false
      window.clearInterval(timer)
    }
  }, [intervalMs, vaultId])

  const subtitle =
    error ?? (
      stats?.active_policies ?
        `${stats.active_policies} active policy${stats.active_policies === 1 ? '' : 'ies'}`
      : 'No active S3 budget policy'
    )

  return (
    <StatsCard
      title="Cost Control"
      subtitle={subtitle}
      right={<SackDollarIcon className="h-5 w-5 fill-current text-cyan-200" />}>
      <div className="grid gap-3 md:grid-cols-4">
        <Stat label="Month spend" value={money(stats?.current_monthly_spend, stats?.currency)} />
        <Stat label="Projection" value={money(stats?.projected_monthly_spend, stats?.currency)} tone="text-amber-100" />
        <Stat label="Warnings" value={stats?.warning_notifications ?? 0} tone={(stats?.warning_notifications ?? 0) > 0 ? 'text-amber-100' : 'text-white'} />
        <Stat label="Critical" value={stats?.critical_notifications ?? 0} tone={(stats?.critical_notifications ?? 0) > 0 ? 'text-red-100' : 'text-white'} />
      </div>
    </StatsCard>
  )
}
