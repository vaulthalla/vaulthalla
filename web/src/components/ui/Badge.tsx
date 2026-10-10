import React from 'react'
import { cn } from '@/util/cn'
import { severityLabel, severityTone, toneClasses, type Tone } from '@/lib/tone'

export const Badge = ({
  tone = 'neutral',
  children,
  className,
  dot = false,
  pulse = false,
  title,
}: {
  tone?: Tone
  children: React.ReactNode
  className?: string
  dot?: boolean
  pulse?: boolean
  title?: string
}) => {
  const t = toneClasses[tone]
  return (
    <span
      title={title}
      className={cn(
        'inline-flex h-6 max-w-full items-center gap-1.5 rounded-full border px-2.5 text-xs font-medium whitespace-nowrap [&_svg]:text-inherit',
        t.bg,
        t.border,
        t.text,
        className,
      )}>
      {dot ? (
        <span className="relative flex size-1.5 shrink-0">
          {pulse ? <span className={cn('absolute inline-flex size-full animate-ping rounded-full opacity-60', t.dot)} /> : null}
          <span className={cn('relative inline-flex size-1.5 rounded-full', t.dot)} />
        </span>
      ) : null}
      <span className="truncate">{children}</span>
    </span>
  )
}

// A backend severity, rendered as-is. Missing severity is "Unknown", never healthy.
export const SeverityBadge = ({ severity, label, className }: { severity?: string | null; label?: string; className?: string }) => (
  <Badge tone={severityTone(severity)} dot className={className}>
    {label ?? severityLabel(severity)}
  </Badge>
)

export const Dot = ({ tone, className, label }: { tone: Tone; className?: string; label?: string }) => (
  <span
    role={label ? 'img' : undefined}
    aria-label={label}
    className={cn('inline-block size-2 shrink-0 rounded-full', toneClasses[tone].dot, className)}
  />
)

export const Kbd = ({ children, className }: { children: React.ReactNode; className?: string }) => (
  <kbd
    className={cn(
      'inline-flex h-5 min-w-5 items-center justify-center rounded border border-line-strong bg-surface-2 px-1 font-mono text-[11px] text-fg-subtle',
      className,
    )}>
    {children}
  </kbd>
)
