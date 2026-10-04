'use client'

import React from 'react'
import { cn } from '@/util/cn'
import { Label } from '@/components/ui/Field'

type CheckedState = boolean | 'indeterminate'

interface CheckboxProps extends Omit<React.InputHTMLAttributes<HTMLInputElement>, 'checked' | 'onChange' | 'type'> {
  checked?: CheckedState
  onCheckedChange?: (checked: boolean) => void
}

// Native checkbox (no JS dependency), styled to the theme. Supports the indeterminate state.
export const Checkbox = React.forwardRef<HTMLInputElement, CheckboxProps>(({ className, checked, onCheckedChange, ...props }, ref) => {
  const inner = React.useRef<HTMLInputElement | null>(null)
  React.useEffect(() => {
    if (inner.current) inner.current.indeterminate = checked === 'indeterminate'
  }, [checked])
  return (
    <input
      ref={node => {
        inner.current = node
        if (typeof ref === 'function') ref(node)
        else if (ref) ref.current = node
      }}
      type="checkbox"
      checked={checked === true}
      onChange={event => onCheckedChange?.(event.target.checked)}
      className={cn(
        'grid size-4 shrink-0 cursor-pointer appearance-none place-items-center rounded-[5px] border border-line-strong bg-black/30 transition-colors hover:border-accent-line disabled:cursor-not-allowed disabled:opacity-50',
        'check-control',
        className,
      )}
      {...props}
    />
  )
})
Checkbox.displayName = 'Checkbox'

interface SwitchProps extends Omit<React.ButtonHTMLAttributes<HTMLButtonElement>, 'onChange'> {
  checked: boolean
  onCheckedChange?: (checked: boolean) => void
}

export const Switch = React.forwardRef<HTMLButtonElement, SwitchProps>(({ className, checked, onCheckedChange, disabled, ...props }, ref) => (
  <button
    ref={ref}
    type="button"
    role="switch"
    aria-checked={checked}
    disabled={disabled}
    onClick={() => onCheckedChange?.(!checked)}
    className={cn(
      'relative inline-flex h-5 w-9 shrink-0 items-center rounded-full border border-line-strong bg-surface-3 transition-colors disabled:opacity-50 aria-checked:border-accent aria-checked:bg-accent',
      className,
    )}
    {...props}>
    <span
      aria-hidden
      className={cn('block size-3.5 translate-x-0.5 rounded-full bg-fg shadow transition-transform', checked && 'translate-x-[18px] bg-accent-ink')}
    />
  </button>
))
Switch.displayName = 'Switch'

// A labelled toggle row: switch on the right, label and hint on the left.
export const SwitchRow = ({
  id,
  label,
  hint,
  checked,
  onCheckedChange,
  disabled,
}: {
  id: string
  label: React.ReactNode
  hint?: React.ReactNode
  checked: boolean
  onCheckedChange: (checked: boolean) => void
  disabled?: boolean
}) => (
  <div className="flex items-start justify-between gap-4 rounded-control border border-line bg-surface-1 px-3 py-2.5">
    <div className="min-w-0">
      <Label htmlFor={id} className="text-fg">
        {label}
      </Label>
      {hint ? <p className="mt-0.5 text-xs text-fg-subtle">{hint}</p> : null}
    </div>
    <Switch id={id} checked={checked} onCheckedChange={onCheckedChange} disabled={disabled} />
  </div>
)
