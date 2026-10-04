'use client'

import React from 'react'
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
