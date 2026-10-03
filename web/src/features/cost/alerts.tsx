'use client'

import React, { useState } from 'react'
import Link from 'next/link'
import { api } from '@/lib/session'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { Badge, SeverityBadge } from '@/components/ui/Badge'
import { Button } from '@/components/ui/Button'
import { Label } from '@/components/ui/Field'
import { Switch } from '@/components/ui/Choice'
import { QueryState } from '@/components/ui/State'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { CheckIcon, XmarkIcon } from '@/components/ui/icons'
import { DASH, formatDateTime, formatRelative, titleCase } from '@/lib/format'
import type { Tone } from '@/lib/tone'
import type { PriceNotification, PriceOverride } from '@/features/cost/model'
import { ackNotification, refreshPricing, useNotifications, useOverrides } from '@/features/cost/queries'
import { EmptyRows, Money, Section } from '@/features/cost/bits'

// Price notifications use info/warning/error/critical; the shared tone map knows all four.
export const NotificationSeverity = ({ severity }: { severity: string | null }) => (
  <SeverityBadge severity={severity} label={severity ? titleCase(severity) : 'Unknown'} />
)

export const AlertsSection = ({
  enabled,
  vaultName,
}: {
  enabled: boolean
  vaultName: (id: number | null) => string | null
}) => {
  const [showAcked, setShowAcked] = useState(false)
  const alerts = useNotifications(enabled, showAcked)
  const columns: Column<PriceNotification>[] = [
    { key: 'severity', header: 'Severity', cell: n => <NotificationSeverity severity={n.severity} /> },
    {
      key: 'alert',
      header: 'Alert',
      cell: n => (
        <div className="min-w-0 py-1.5">
          <div className="text-fg font-medium">{n.title ?? titleCase(n.type ?? 'Alert')}</div>
          {n.message ?
            <div className="text-fg-subtle max-w-xl text-xs">{n.message}</div>
          : null}
        </div>
      ),
    },
    {
      key: 'vault',
      header: 'Vault',
      hideBelow: 'md',
      cell: n =>
        n.vault_id ?
          <Link href={`/vaults/${n.vault_id}`} className="text-accent-text hover:underline">
            {vaultName(n.vault_id)}
          </Link>
        : <span className="text-fg-faint">{DASH}</span>,
    },
    {
      key: 'when',
      header: 'Raised',
      hideBelow: 'sm',
      sortValue: n => n.created_at,
      cell: n => (
        <span className="tabular text-fg-subtle whitespace-nowrap" title={formatDateTime(n.created_at)}>
          {formatRelative(n.created_at)}
        </span>
      ),
    },
  ]
  return (
    <Section
      id="budget-alerts"
      title="Budget alerts"
      description="Raised when a budget window is about to run out or a sync was blocked."
      actions={
        <div className="flex items-center gap-2">
          <Switch id="show-acked" checked={showAcked} onCheckedChange={setShowAcked} />
          <Label htmlFor="show-acked" className="text-fg-subtle">
            Show acknowledged
          </Label>
        </div>
      }>
      <QueryState query={alerts}>
        {rows =>
          rows.length === 0 ?
            <EmptyRows>{showAcked ? 'No budget alerts have been raised.' : 'No open budget alerts.'}</EmptyRows>
          : <DataTable
              rows={rows}
              columns={columns}
              rowKey={n => n.id}
              initialSort={{ key: 'when', dir: 'desc' }}
              rowActions={n =>
                n.acknowledged_at ?
                  <span className="text-fg-faint px-2 text-xs">Acknowledged</span>
                : <Button size="sm" variant="ghost" onClick={() => void ackNotification(n)}>
                    <CheckIcon aria-hidden />
                    Ack
                  </Button>
              }
            />
        }
      </QueryState>
    </Section>
  )
}

const OVERRIDE_TONE: Record<string, Tone> = { requested: 'accent' }

const approve = async (o: PriceOverride, vaultName: string | null) => {
  const ok = await confirm({
    title: `Approve override #${o.id}?`,
    description: `The next sync of ${vaultName ?? 'this vault'} may exceed its enforced budget once${o.estimated_cost ? ` (estimated ${o.estimated_cost} ${o.currency})` : ''}.`,
    confirmLabel: 'Approve',
    tone: 'primary',
  })
  if (!ok) return
  try {
    await api.send('pricing.budget.override.approve', { id: o.id })
    await refreshPricing()
    notify.success('Override approved')
  } catch (error) {
    notify.error(error, 'Could not approve the override')
  }
}

const deny = async (o: PriceOverride, vaultName: string | null) => {
  const ok = await confirm({
    title: `Deny override #${o.id}?`,
    description: `The blocked sync of ${vaultName ?? 'this vault'} stays blocked until spend fits the budget or a new override is approved.`,
    confirmLabel: 'Deny',
  })
  if (!ok) return
  try {
    await api.send('pricing.budget.override.deny', { id: o.id })
    await refreshPricing()
    notify.success('Override denied')
  } catch (error) {
    notify.error(error, 'Could not deny the override')
  }
}

export const OverridesSection = ({
  enabled,
  vaultName,
}: {
  enabled: boolean
  vaultName: (id: number | null) => string | null
}) => {
  const overrides = useOverrides(enabled)
  const columns: Column<PriceOverride>[] = [
    {
      key: 'request',
      header: 'Request',
      cell: o => (
        <div className="min-w-0 py-1.5">
          <div className="text-fg font-medium">
            <span className="tabular text-fg-subtle">#{o.id}</span> {vaultName(o.vault_id) ?? DASH}
          </div>
          {o.reason ?
            <div className="text-fg-subtle max-w-md text-xs">“{o.reason}”</div>
          : null}
        </div>
      ),
    },
    {
      key: 'status',
      header: 'Status',
      cell: o => <Badge tone={OVERRIDE_TONE[o.status] ?? 'neutral'}>{titleCase(o.status)}</Badge>,
    },
    {
      key: 'cost',
      header: 'Estimate',
      hideBelow: 'sm',
      cell: o => <Money value={o.estimated_cost} currency={o.currency} />,
    },
    {
      key: 'when',
      header: 'Requested',
      hideBelow: 'md',
      sortValue: o => o.created_at,
      cell: o => (
        <span className="tabular text-fg-subtle whitespace-nowrap" title={formatDateTime(o.created_at)}>
          {formatRelative(o.created_at)}
        </span>
      ),
    },
    {
      key: 'expires',
      header: 'Expires',
      hideBelow: 'lg',
      cell: o => <span className="tabular text-fg-subtle whitespace-nowrap">{formatDateTime(o.expires_at)}</span>,
    },
  ]
  return (
    <Section
      id="overrides"
      title="Override requests"
      description="A one-time pass for a sync an enforced budget blocked. Only super admins can decide.">
      <QueryState query={overrides}>
        {rows =>
          rows.length === 0 ?
            <EmptyRows>No override requests.</EmptyRows>
          : <DataTable
              rows={rows}
              columns={columns}
              rowKey={o => o.id}
              initialSort={{ key: 'when', dir: 'desc' }}
              rowActions={o =>
                o.status === 'requested' ?
                  <div className="flex justify-end gap-1">
                    <Button size="sm" variant="subtle" onClick={() => void approve(o, vaultName(o.vault_id))}>
                      <CheckIcon aria-hidden />
                      Approve
                    </Button>
                    <Button
                      size="sm"
                      variant="ghost"
                      onClick={() => void deny(o, vaultName(o.vault_id))}
                      aria-label={`Deny override ${o.id}`}>
                      <XmarkIcon aria-hidden />
                      <span className="hidden sm:inline">Deny</span>
                    </Button>
                  </div>
                : null
              }
            />
        }
      </QueryState>
    </Section>
  )
}
