'use client'

import React from 'react'
import { Checkbox } from '@/components/ui/Choice'

// A labelled checkbox row with a hint (a native checkbox, so tests and keyboards treat it as one).
export const CheckRow = ({
  id,
  label,
  hint,
  checked,
  onCheckedChange,
  disabled,
  testId,
}: {
  id: string
  label: React.ReactNode
  hint?: React.ReactNode
  checked: boolean
  onCheckedChange: (checked: boolean) => void
  disabled?: boolean
  testId?: string
}) => (
  <label
    htmlFor={id}
    className="rounded-control border-line bg-surface-1 flex cursor-pointer items-start gap-3 border px-3 py-2.5 has-[:disabled]:cursor-not-allowed">
    <Checkbox
      id={id}
      data-testid={testId}
      checked={checked}
      onCheckedChange={onCheckedChange}
      disabled={disabled}
      className="mt-0.5"
    />
    <span className="min-w-0">
      <span className="text-fg block text-[13px] font-medium">{label}</span>
      {hint ?
        <span className="text-fg-subtle mt-0.5 block text-xs">{hint}</span>
      : null}
    </span>
  </label>
)
