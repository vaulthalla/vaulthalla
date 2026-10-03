'use client'

import React, { useState } from 'react'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Field, Input, SwitchRow } from '@/components/ui/Field'
import { Segmented } from '@/components/ui/Tabs'
import { InlineError } from '@/components/ui/State'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { MoneyInput, ScaledInput, SECOND_UNITS, isDecimal } from '@/features/settings/controls'
import {
  DEFAULT_CATALOG_AGE_SECONDS,
  MODES,
  type BudgetMode,
  type BudgetPolicy,
  type BudgetScope,
  type PolicyPayload,
} from '@/features/cost/model'

export interface PolicyTarget {
  scope: BudgetScope
  title: string
  provider_key?: string | null
  vault_id?: number | null
  gateway_credential_id?: number | null
}

interface Draft {
  mode: BudgetMode
  currency: string
  run: string
  daily: string
  monthly: string
  requireVerified: boolean
  allowStale: boolean
  maxAge: number | null
}

const draftFrom = (policy: BudgetPolicy | null): Draft => ({
  mode: policy?.is_active ? policy.mode : 'off',
  currency: policy?.currency ?? 'USD',
  run: policy?.max_run_cost ?? '',
  daily: policy?.max_daily_cost ?? '',
  monthly: policy?.max_monthly_cost ?? '',
  requireVerified: policy?.require_verified_catalog ?? true,
  allowStale: policy?.allow_stale_catalog ?? false,
  maxAge: policy ? policy.max_catalog_age_seconds : DEFAULT_CATALOG_AGE_SECONDS,
})

const limitError = (value: string) =>
  value.trim() && !isDecimal(value) ? 'Use a plain amount, e.g. 25 or 12.50' : undefined

export const PolicyDialog = ({
  target,
  policy,
  onClose,
  onSave,
  onDisable,
  windows = ['run', 'daily', 'monthly'],
}: {
  target: PolicyTarget | null
  policy: BudgetPolicy | null
  onClose: () => void
  onSave: (payload: PolicyPayload) => Promise<unknown>
  onDisable?: (target: PolicyTarget) => Promise<unknown>
  windows?: ('run' | 'daily' | 'monthly')[]
}) => (
  <Dialog open={Boolean(target)} onOpenChange={open => !open && onClose()}>
    {target ?
      <PolicyForm
        key={`${target.scope}:${target.provider_key ?? ''}:${target.vault_id ?? ''}:${target.gateway_credential_id ?? ''}`}
        target={target}
        policy={policy}
        onClose={onClose}
        onSave={onSave}
        onDisable={onDisable}
        windows={windows}
      />
    : null}
  </Dialog>
)

const PolicyForm = ({
  target,
  policy,
  onClose,
  onSave,
  onDisable,
  windows,
}: {
  target: PolicyTarget
  policy: BudgetPolicy | null
  onClose: () => void
  onSave: (payload: PolicyPayload) => Promise<unknown>
  onDisable?: (target: PolicyTarget) => Promise<unknown>
  windows: ('run' | 'daily' | 'monthly')[]
}) => {
  const [draft, setDraft] = useState<Draft>(() => draftFrom(policy))
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)
  const set = <K extends keyof Draft>(key: K, value: Draft[K]) => setDraft(current => ({ ...current, [key]: value }))

  const errors = {
    currency: /^[A-Za-z0-9]{3,8}$/.test(draft.currency) ? undefined : 'Use a 3–8 letter currency code',
    run: limitError(draft.run),
    daily: limitError(draft.daily),
    monthly: limitError(draft.monthly),
    maxAge: draft.maxAge !== null && draft.maxAge <= 0 ? 'Must be more than zero' : undefined,
  }
  const invalid = Object.values(errors).some(Boolean)
  const noLimits = draft.mode !== 'off' && !draft.run.trim() && !draft.daily.trim() && !draft.monthly.trim()
  const modeHint = MODES.find(m => m.value === draft.mode)?.hint

  const save = async (event: React.FormEvent) => {
    event.preventDefault()
    if (invalid) return
    setBusy(true)
    setError(null)
    try {
      await onSave({
        scope: target.scope,
        provider_key: target.provider_key ?? null,
        vault_id: target.vault_id ?? null,
        gateway_credential_id: target.gateway_credential_id ?? null,
        mode: draft.mode,
        currency: draft.currency.toUpperCase(),
        max_run_cost: draft.run.trim() || null,
        max_daily_cost: draft.daily.trim() || null,
        max_monthly_cost: draft.monthly.trim() || null,
        require_verified_catalog: draft.requireVerified,
        allow_stale_catalog: draft.allowStale,
        max_catalog_age_seconds: draft.maxAge,
      })
      notify.success('Budget saved', target.title)
      onClose()
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const disable = async () => {
    if (!onDisable) return
    const ok = await confirm({
      title: `Disable the ${target.title} budget?`,
      description: 'Its limits stop applying immediately. Spend already recorded stays in the ledger.',
      confirmLabel: 'Disable budget',
    })
    if (!ok) return
    setBusy(true)
    try {
      await onDisable(target)
      notify.success('Budget disabled', target.title)
      onClose()
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const prefix = `policy-${target.scope}`

  return (
    <DialogContent
      size="lg"
      title={`Budget · ${target.title}`}
      description={policy?.is_active ? 'Active budget' : 'No active budget — saving one starts applying it.'}
      footer={
        <>
          {onDisable && policy?.is_active ?
            <Button variant="danger" className="mr-auto" onClick={disable} disabled={busy}>
              Disable
            </Button>
          : null}
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button type="submit" form={`${prefix}-form`} variant="primary" loading={busy} disabled={invalid}>
            Save budget
          </Button>
        </>
      }>
      <form id={`${prefix}-form`} onSubmit={save} className="space-y-5">
        <div className="space-y-2">
          <div className="text-fg-muted text-[13px] font-medium">Mode</div>
          <Segmented
            label="Mode"
            value={draft.mode}
            onChange={value => set('mode', value)}
            options={MODES.map(m => ({ value: m.value, label: m.label }))}
          />
          {modeHint ?
            <p className="text-fg-subtle text-xs">{modeHint}</p>
          : null}
        </div>

        <div className="grid gap-4 sm:grid-cols-[8rem_1fr]">
          <Field label="Currency" htmlFor={`${prefix}-currency`} error={errors.currency}>
            <Input
              id={`${prefix}-currency`}
              value={draft.currency}
              maxLength={8}
              onChange={event => set('currency', event.target.value.toUpperCase())}
              className="font-mono uppercase"
            />
          </Field>
          <div className="grid gap-4 sm:grid-cols-3">
            {windows.includes('run') ?
              <Field label="Per sync run" htmlFor={`${prefix}-run`} error={errors.run}>
                <MoneyInput
                  id={`${prefix}-run`}
                  value={draft.run}
                  currency={draft.currency}
                  onChange={v => set('run', v)}
                  invalid={Boolean(errors.run)}
                />
              </Field>
            : null}
            {windows.includes('daily') ?
              <Field label="Per day" htmlFor={`${prefix}-daily`} error={errors.daily}>
                <MoneyInput
                  id={`${prefix}-daily`}
                  value={draft.daily}
                  currency={draft.currency}
                  onChange={v => set('daily', v)}
                  invalid={Boolean(errors.daily)}
                />
              </Field>
            : null}
            {windows.includes('monthly') ?
              <Field label="Per month" htmlFor={`${prefix}-monthly`} error={errors.monthly}>
                <MoneyInput
                  id={`${prefix}-monthly`}
                  value={draft.monthly}
                  currency={draft.currency}
                  onChange={v => set('monthly', v)}
                  invalid={Boolean(errors.monthly)}
                />
              </Field>
            : null}
          </div>
        </div>
        {noLimits ?
          <p className="text-fg-subtle -mt-2 text-xs">With no limits set, this budget only records spend.</p>
        : null}

        <fieldset className="space-y-2">
          <legend className="text-fg-muted mb-2 text-[13px] font-medium">Price catalog</legend>
          <SwitchRow
            id={`${prefix}-verified`}
            label="Require a verified catalog"
            hint="Estimates from an unverified price catalog count as unknown cost."
            checked={draft.requireVerified}
            onCheckedChange={v => set('requireVerified', v)}
          />
          <SwitchRow
            id={`${prefix}-stale`}
            label="Allow a stale catalog"
            hint="Use prices older than the maximum age below instead of treating them as unknown."
            checked={draft.allowStale}
            onCheckedChange={v => set('allowStale', v)}
          />
          <Field
            label="Maximum catalog age"
            htmlFor={`${prefix}-age`}
            error={errors.maxAge}
            className="pt-1 sm:max-w-xs">
            <ScaledInput
              id={`${prefix}-age`}
              value={draft.maxAge}
              onChange={v => set('maxAge', v)}
              units={SECOND_UNITS.slice(2)}
            />
          </Field>
        </fieldset>

        <InlineError error={error} />
      </form>
    </DialogContent>
  )
}
