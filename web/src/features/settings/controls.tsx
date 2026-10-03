'use client'

import React, { useState } from 'react'
import { cn } from '@/util/cn'
import { Input } from '@/components/ui/Field'
import { XmarkIcon } from '@/components/ui/icons'

const adornment = 'pointer-events-none absolute top-1/2 -translate-y-1/2 text-xs text-fg-subtle'

// A number with its unit spelled out next to it ("60 minutes"). Empty input reports null.
export const UnitInput = ({
  id,
  value,
  onChange,
  unit,
  min,
  max,
  step,
  disabled,
  invalid,
  placeholder,
  className,
}: {
  id: string
  value: number | null
  onChange: (value: number | null) => void
  unit?: string
  min?: number
  max?: number
  step?: number | 'any'
  disabled?: boolean
  invalid?: boolean
  placeholder?: string
  className?: string
}) => (
  <div className={cn('relative', className)}>
    <Input
      id={id}
      type="number"
      inputMode={step === 'any' ? 'decimal' : 'numeric'}
      min={min}
      max={max}
      step={step ?? 1}
      value={value ?? ''}
      placeholder={placeholder}
      disabled={disabled}
      aria-invalid={invalid || undefined}
      onChange={event => {
        const raw = event.target.value
        if (raw.trim() === '') return onChange(null)
        const n = Number(raw)
        onChange(Number.isFinite(n) ? n : null)
      }}
      className={cn('tabular', unit && 'pr-20')}
    />
    {unit ?
      <span className={cn(adornment, 'right-3')}>{unit}</span>
    : null}
  </div>
)

// A decimal amount kept as a string (budgets and catalog prices are decimal strings in core).
export const MoneyInput = ({
  id,
  value,
  onChange,
  currency = 'USD',
  disabled,
  invalid,
  placeholder = 'No limit',
}: {
  id: string
  value: string
  onChange: (value: string) => void
  currency?: string
  disabled?: boolean
  invalid?: boolean
  placeholder?: string
}) => (
  <div className="relative">
    <Input
      id={id}
      inputMode="decimal"
      autoComplete="off"
      value={value}
      placeholder={placeholder}
      disabled={disabled}
      aria-invalid={invalid || undefined}
      onChange={event => onChange(event.target.value)}
      className="tabular pr-14"
    />
    <span className={cn(adornment, 'right-3 font-mono')}>{currency}</span>
  </div>
)

export const isDecimal = (value: string) => /^\d+(\.\d{1,8})?$/.test(value.trim())

type Unit = { label: string; factor: number }

// A quantity stored in a base unit (bytes, seconds, …) and edited in the largest unit that divides it evenly.
export const ScaledInput = ({
  id,
  value,
  onChange,
  units,
  disabled,
  invalid,
  min = 0,
}: {
  id: string
  value: number | null
  onChange: (value: number | null) => void
  units: Unit[]
  disabled?: boolean
  invalid?: boolean
  min?: number
}) => {
  const fitting = (v: number | null) => {
    if (v === null || v === 0) return units[0]
    return [...units].reverse().find(u => v % u.factor === 0) ?? units[0]
  }
  const [unit, setUnit] = useState<Unit>(() => fitting(value))
  const shown = value === null ? '' : String(+(value / unit.factor).toFixed(6))
  return (
    <div className="flex">
      <Input
        id={id}
        type="number"
        inputMode="decimal"
        min={min}
        step="any"
        value={shown}
        disabled={disabled}
        aria-invalid={invalid || undefined}
        onChange={event => {
          const raw = event.target.value
          if (raw.trim() === '') return onChange(null)
          const n = Number(raw)
          onChange(Number.isFinite(n) ? Math.round(n * unit.factor) : null)
        }}
        className="tabular rounded-r-none"
      />
      <select
        aria-label="Unit"
        value={unit.label}
        disabled={disabled}
        onChange={event => setUnit(units.find(u => u.label === event.target.value) ?? units[0])}
        className="rounded-r-control border-line-strong bg-surface-2 text-fg-muted focus:border-accent-line h-9 border border-l-0 px-2 text-xs focus:outline-none">
        {units.map(u => (
          <option key={u.label} value={u.label}>
            {u.label}
          </option>
        ))}
      </select>
    </div>
  )
}

export const BYTE_UNITS: Unit[] = [
  { label: 'MB', factor: 1024 ** 2 },
  { label: 'GB', factor: 1024 ** 3 },
  { label: 'TB', factor: 1024 ** 4 },
]
export const SECOND_UNITS: Unit[] = [
  { label: 'seconds', factor: 1 },
  { label: 'minutes', factor: 60 },
  { label: 'hours', factor: 3600 },
  { label: 'days', factor: 86_400 },
]

// A list of short values as removable chips. Enter, comma or Tab adds the typed value.
export const TagInput = ({
  id,
  value,
  onChange,
  placeholder = 'Add…',
  validate,
  disabled,
  mono,
}: {
  id: string
  value: string[]
  onChange: (value: string[]) => void
  placeholder?: string
  validate?: (item: string) => string | null
  disabled?: boolean
  mono?: boolean
}) => {
  const [draft, setDraft] = useState('')
  const [error, setError] = useState<string | null>(null)
  const commit = () => {
    const item = draft.trim().replace(/,$/, '')
    if (!item) return false
    const problem = validate?.(item) ?? null
    if (problem) {
      setError(problem)
      return true
    }
    if (!value.includes(item)) onChange([...value, item])
    setDraft('')
    setError(null)
    return true
  }
  return (
    <div>
      <div
        className={cn(
          'rounded-control border-line-strong focus-within:border-accent-line flex min-h-9 flex-wrap items-center gap-1.5 border bg-black/30 px-2 py-1.5',
          error && 'border-danger-line',
          disabled && 'opacity-50',
        )}>
        {value.map(item => (
          <span
            key={item}
            className={cn(
              'border-line bg-surface-2 text-fg inline-flex h-6 items-center gap-1 rounded-md border pr-1 pl-2 text-xs',
              mono && 'font-mono',
            )}>
            {item}
            <button
              type="button"
              disabled={disabled}
              aria-label={`Remove ${item}`}
              onClick={() => onChange(value.filter(v => v !== item))}
              className="text-fg-subtle hover:bg-surface-3 hover:text-fg rounded p-0.5 [&_svg]:size-3">
              <XmarkIcon aria-hidden />
            </button>
          </span>
        ))}
        <input
          id={id}
          value={draft}
          disabled={disabled}
          placeholder={value.length ? '' : placeholder}
          onChange={event => {
            setDraft(event.target.value)
            setError(null)
          }}
          onKeyDown={event => {
            if ((event.key === 'Enter' || event.key === ',' || (event.key === 'Tab' && draft.trim())) && commit())
              event.preventDefault()
            if (event.key === 'Backspace' && !draft && value.length) onChange(value.slice(0, -1))
          }}
          onBlur={() => commit()}
          className={cn(
            'text-fg placeholder:text-fg-faint h-6 min-w-24 flex-1 bg-transparent text-sm focus:outline-none',
            mono && 'font-mono',
          )}
        />
      </div>
      {error ?
        <p role="alert" className="text-danger mt-1 text-xs">
          {error}
        </p>
      : null}
    </div>
  )
}

export const EMAIL_RE = /^[^\s@<>]+@[^\s@<>]+\.[^\s@<>]+$/
