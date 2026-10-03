'use client'

import React from 'react'
import { useFormContext } from 'react-hook-form'
import { Input, Select } from '@/components/ui/Field'

// Form pieces shared by "New vault" and vault settings.

export type QuotaUnit = 'GiB' | 'TiB' | 'MiB'
const UNIT_BYTES: Record<QuotaUnit, number> = { MiB: 1024 ** 2, GiB: 1024 ** 3, TiB: 1024 ** 4 }

export interface QuotaValues {
  quota_amount: string
  quota_unit: QuotaUnit
}

// 0 (the daemon's "unlimited") becomes an empty amount.
export const quotaDefaults = (bytes: number | null | undefined): QuotaValues => {
  if (!bytes || bytes <= 0) return { quota_amount: '', quota_unit: 'GiB' }
  for (const unit of ['TiB', 'GiB', 'MiB'] as const)
    if (bytes % UNIT_BYTES[unit] === 0) return { quota_amount: String(bytes / UNIT_BYTES[unit]), quota_unit: unit }
  return { quota_amount: String(Math.round((bytes / UNIT_BYTES.GiB) * 100) / 100), quota_unit: 'GiB' }
}

export const quotaBytes = ({ quota_amount, quota_unit }: QuotaValues) => {
  const amount = Number(quota_amount.trim() || 0)
  return Number.isFinite(amount) && amount > 0 ? Math.round(amount * UNIT_BYTES[quota_unit]) : 0
}

export const QuotaInput = ({ id, disabled }: { id: string; disabled?: boolean }) => {
  const { register, formState } = useFormContext<QuotaValues>()
  return (
    <div className="flex gap-2">
      <Input
        id={id}
        inputMode="decimal"
        placeholder="Unlimited"
        disabled={disabled}
        className="tabular"
        aria-invalid={Boolean(formState.errors.quota_amount)}
        {...register('quota_amount', {
          validate: value => !value.trim() || (Number.isFinite(Number(value)) && Number(value) >= 0) || 'Enter a size, or leave empty for no quota',
        })}
      />
      <div className="w-24 shrink-0">
        <Select aria-label="Quota unit" disabled={disabled} {...register('quota_unit')}>
          <option value="MiB">MiB</option>
          <option value="GiB">GiB</option>
          <option value="TiB">TiB</option>
        </Select>
      </div>
    </div>
  )
}

// FUSE directory names are one path component (the daemon refuses separators and dot names).
export const validateFuseName = (value: string | null | undefined) => {
  const clean = (value ?? '').trim()
  if (!clean) return true
  if (clean === '.' || clean === '..' || /[/\\\0]/.test(clean)) return 'Use a single folder name: no slashes, not . or ..'
  return true
}

// S3-safe slug: what the gateway and FUSE derive names from.
export const SLUG_PATTERN = /^[a-z0-9][a-z0-9-]{1,61}[a-z0-9]$/

export const storageTierOptions = (provider?: string) => {
  if (provider === 'AWS')
    return [
      { value: '', label: 'Provider default' },
      { value: 'standard', label: 'Standard' },
      { value: 'standard_ia', label: 'Standard-IA' },
    ]
  if (provider === 'Cloudflare R2')
    return [
      { value: '', label: 'Provider default' },
      { value: 'standard', label: 'Standard' },
      { value: 'infrequent_access', label: 'Infrequent Access' },
    ]
  return null
}
