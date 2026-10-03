'use client'

import React from 'react'
import * as RCheckbox from '@radix-ui/react-checkbox'
import * as RSwitch from '@radix-ui/react-switch'
import { cn } from '@/util/cn'

const control =
  'w-full rounded-control border border-line-strong bg-black/30 px-3 text-sm text-fg placeholder:text-fg-faint transition-[border-color,box-shadow] duration-150 hover:border-white/25 focus:border-accent-line focus:outline-none focus:shadow-[0_0_0_3px_rgb(34_211_238/0.15)] disabled:cursor-not-allowed disabled:opacity-50 aria-[invalid=true]:border-danger-line'

export const Input = React.forwardRef<HTMLInputElement, React.InputHTMLAttributes<HTMLInputElement>>(
  ({ className, ...props }, ref) => <input ref={ref} className={cn(control, 'h-9', className)} {...props} />,
)
Input.displayName = 'Input'

export const Textarea = React.forwardRef<HTMLTextAreaElement, React.TextareaHTMLAttributes<HTMLTextAreaElement>>(
  ({ className, ...props }, ref) => <textarea ref={ref} className={cn(control, 'min-h-20 py-2', className)} {...props} />,
)
Textarea.displayName = 'Textarea'

export const Select = React.forwardRef<HTMLSelectElement, React.SelectHTMLAttributes<HTMLSelectElement>>(
  ({ className, children, ...props }, ref) => (
    <div className="relative">
      <select ref={ref} className={cn(control, 'h-9 appearance-none pr-8', className)} {...props}>
        {children}
      </select>
      <svg
        aria-hidden
        viewBox="0 0 16 16"
        className="pointer-events-none absolute top-1/2 right-2.5 size-3.5 -translate-y-1/2 text-fg-subtle">
        <path d="M4 6l4 4 4-4" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
      </svg>
    </div>
  ),
)
Select.displayName = 'Select'

export const Label = ({ className, ...props }: React.LabelHTMLAttributes<HTMLLabelElement>) => (
  <label className={cn('text-[13px] font-medium text-fg-muted', className)} {...props} />
)

interface FieldProps {
  label?: React.ReactNode
  htmlFor?: string
  hint?: React.ReactNode
  error?: string
  required?: boolean
  className?: string
  children: React.ReactNode
}

// Label + control + hint/error. Pass the control's id as htmlFor so the label is wired.
export const Field = ({ label, htmlFor, hint, error, required, className, children }: FieldProps) => (
  <div className={cn('flex flex-col gap-1.5', className)}>
    {label ? (
      <Label htmlFor={htmlFor}>
        {label}
        {required ? <span className="ml-0.5 text-accent-text">*</span> : null}
      </Label>
    ) : null}
    {children}
    {error ? (
      <p role="alert" className="text-xs text-danger">
        {error}
      </p>
    ) : hint ? (
      <p className="text-xs text-fg-subtle">{hint}</p>
    ) : null}
  </div>
)

export const Checkbox = React.forwardRef<
  React.ElementRef<typeof RCheckbox.Root>,
  React.ComponentPropsWithoutRef<typeof RCheckbox.Root>
>(({ className, ...props }, ref) => (
  <RCheckbox.Root
    ref={ref}
    className={cn(
      'grid size-4 shrink-0 place-items-center rounded-[5px] border border-line-strong bg-black/30 transition-colors hover:border-accent-line data-[state=checked]:border-accent data-[state=checked]:bg-accent data-[state=indeterminate]:border-accent data-[state=indeterminate]:bg-accent disabled:opacity-50',
      className,
    )}
    {...props}>
    <RCheckbox.Indicator className="text-accent-ink">
      {props.checked === 'indeterminate' ? (
        <svg viewBox="0 0 12 12" className="size-3">
          <path d="M3 6h6" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
        </svg>
      ) : (
        <svg viewBox="0 0 12 12" className="size-3">
          <path d="M2.5 6.2l2.3 2.3 4.7-5" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
        </svg>
      )}
    </RCheckbox.Indicator>
  </RCheckbox.Root>
))
Checkbox.displayName = 'Checkbox'

export const Switch = React.forwardRef<
  React.ElementRef<typeof RSwitch.Root>,
  React.ComponentPropsWithoutRef<typeof RSwitch.Root>
>(({ className, ...props }, ref) => (
  <RSwitch.Root
    ref={ref}
    className={cn(
      'relative inline-flex h-5 w-9 shrink-0 items-center rounded-full border border-line-strong bg-surface-3 transition-colors data-[state=checked]:border-accent data-[state=checked]:bg-accent disabled:opacity-50',
      className,
    )}
    {...props}>
    <RSwitch.Thumb className="block size-3.5 translate-x-0.5 rounded-full bg-fg shadow transition-transform data-[state=checked]:translate-x-[18px] data-[state=checked]:bg-accent-ink" />
  </RSwitch.Root>
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
