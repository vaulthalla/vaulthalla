'use client'

import React, { useId } from 'react'
import { useFormContext, useWatch } from 'react-hook-form'
import { Field, Input, Select } from '@/components/ui/Field'
import { SwitchRow } from '@/components/ui/Choice'
import { formatBytes, formatInt } from '@/lib/format'
import {
  BUDGET_FIELDS,
  BUDGET_PRESETS,
  CONFLICT_OPTIONS,
  INTERVAL_CHOICES,
  PRESET_OPTIONS,
  STRATEGY_OPTIONS,
  type BudgetPreset,
  type SyncFormValues,
} from '@/features/vaults/syncPolicy'
import { cn } from '@/util/cn'

// The sync policy and S3 request guardrails, shared by "New vault" and the Sync & cost tab. Lives inside a
// react-hook-form FormProvider whose values carry `sync: SyncFormValues`.

type WithSync = { sync: SyncFormValues }

export const SyncPolicyFields = ({ disabled }: { disabled?: boolean }) => {
  const { register, control, setValue } = useFormContext<WithSync>()
  const ids = useId()
  const strategy = useWatch({ control, name: 'sync.strategy' })
  const conflictPolicy = useWatch({ control, name: 'sync.conflict_policy' })
  const interval = useWatch({ control, name: 'sync.interval_seconds' })
  const enabled = useWatch({ control, name: 'sync.enabled' })
  const custom = !INTERVAL_CHOICES.some(c => c.value === Number(interval))

  return (
    <div className="space-y-5">
      <SwitchRow
        id={`${ids}-enabled`}
        label="Scheduled sync"
        hint="When off, the vault only syncs when someone presses “Sync now”."
        checked={enabled}
        disabled={disabled}
        onCheckedChange={value => setValue('sync.enabled', value, { shouldDirty: true })}
      />

      <div className="grid gap-4 sm:grid-cols-2">
        <Field label="Strategy" htmlFor={`${ids}-strategy`} hint={STRATEGY_OPTIONS.find(o => o.value === strategy)?.hint}>
          <Select id={`${ids}-strategy`} disabled={disabled} {...register('sync.strategy')}>
            {STRATEGY_OPTIONS.map(o => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
          </Select>
        </Field>
        <Field label="On conflict" htmlFor={`${ids}-conflict`} hint={CONFLICT_OPTIONS.find(o => o.value === conflictPolicy)?.hint ?? 'Which copy wins when a file changed on both sides.'}>
          <Select id={`${ids}-conflict`} disabled={disabled} {...register('sync.conflict_policy')}>
            {CONFLICT_OPTIONS.map(o => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
          </Select>
        </Field>
        <Field label="Interval" htmlFor={`${ids}-interval`} hint={custom ? `Custom: every ${formatInt(interval)} s` : 'How often a scheduled sync runs.'}>
          <Select
            id={`${ids}-interval`}
            disabled={disabled}
            value={custom ? 'custom' : String(interval)}
            onChange={event => {
              const value = event.target.value
              setValue('sync.interval_seconds', value === 'custom' ? Number(interval) || 120 : Number(value), { shouldDirty: true })
            }}>
            {INTERVAL_CHOICES.map(c => (
              <option key={c.value} value={c.value}>
                {c.label}
              </option>
            ))}
            {custom ? <option value="custom">Every {formatInt(interval)} s</option> : null}
          </Select>
        </Field>
        <Field label="Max remote index age (seconds)" htmlFor={`${ids}-index-age`} hint="Re-list the bucket when the cached index is older than this. Empty = never.">
          <Input id={`${ids}-index-age`} inputMode="numeric" placeholder="No limit" disabled={disabled} className="tabular" {...register('sync.max_remote_index_age_seconds', { pattern: { value: /^\s*\d*\s*$/, message: 'Whole seconds, or empty' } })} />
        </Field>
      </div>
    </div>
  )
}

export const RequestGuardrailFields = ({ disabled }: { disabled?: boolean }) => {
  const { register, control, setValue } = useFormContext<WithSync>()
  const ids = useId()
  const preset = useWatch({ control, name: 'sync.preset' }) as BudgetPreset

  const choose = (next: BudgetPreset) => {
    setValue('sync.preset', next, { shouldDirty: true })
    if (next !== 'custom') {
      const budget = BUDGET_PRESETS[next]
      for (const { key } of BUDGET_FIELDS) setValue(`sync.budget.${key}`, budget[key] === null ? '' : String(budget[key]), { shouldDirty: true })
    }
  }

  return (
    <div className="space-y-4">
      <div role="radiogroup" aria-label="Request budget preset" className="grid gap-2 sm:grid-cols-5">
        {PRESET_OPTIONS.map(option => (
          <button
            key={option.value}
            type="button"
            role="radio"
            aria-checked={preset === option.value}
            disabled={disabled}
            onClick={() => choose(option.value)}
            className={cn(
              'flex flex-col items-start justify-start rounded-card border border-line bg-surface-1 px-3 py-2.5 text-left transition-colors hover:border-line-strong disabled:opacity-50',
              preset === option.value && 'border-accent-line bg-accent-soft',
            )}>
            <span className={cn('block text-sm font-medium', preset === option.value ? 'text-accent-text' : 'text-fg')}>{option.label}</span>
            <span className="mt-0.5 block text-xs leading-snug text-fg-subtle">{option.hint}</span>
          </button>
        ))}
      </div>
      <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
        {BUDGET_FIELDS.map(field => (
          <Field key={field.key} label={field.label} htmlFor={`${ids}-${field.key}`}>
            {preset === 'custom' ? (
              <Input
                id={`${ids}-${field.key}`}
                inputMode="numeric"
                placeholder="Unlimited"
                disabled={disabled}
                className="tabular"
                {...register(`sync.budget.${field.key}`, { pattern: { value: /^\s*\d*\s*$/, message: 'A whole number, or empty for unlimited' } })}
              />
            ) : (
              <PresetValue id={`${ids}-${field.key}`} value={preset === 'unlimited' ? null : BUDGET_PRESETS[preset][field.key]} bytes={field.bytes} />
            )}
          </Field>
        ))}
      </div>
      <p className="text-xs text-fg-subtle">
        Limits apply to each sync run. A run that would exceed one stops and reports it instead of running up the bill.
        {preset === 'custom' ? ' Downloaded bytes are in bytes; leave a field empty for no limit.' : ''}
      </p>
    </div>
  )
}

const PresetValue = ({ id, value, bytes }: { id: string; value: number | null; bytes?: boolean }) => (
  <output id={id} className="flex h-9 items-center rounded-control border border-line bg-surface-1 px-3 text-sm text-fg-muted tabular">
    {value === null ? 'Unlimited' : bytes ? formatBytes(value) : formatInt(value)}
  </output>
)
