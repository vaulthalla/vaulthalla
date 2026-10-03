import { cn } from '@/util/cn'

export const Spinner = ({ className, label }: { className?: string; label?: string }) => (
  <svg
    className={cn('size-5 animate-spin text-accent-text', className)}
    viewBox="0 0 24 24"
    fill="none"
    role={label ? 'status' : undefined}
    aria-label={label}
    aria-hidden={label ? undefined : true}>
    <circle cx="12" cy="12" r="9" stroke="currentColor" strokeOpacity="0.18" strokeWidth="3" />
    <path d="M21 12a9 9 0 0 0-9-9" stroke="currentColor" strokeWidth="3" strokeLinecap="round" />
  </svg>
)

export const PageSpinner = ({ label = 'Loading' }: { label?: string }) => (
  <div className="flex min-h-[40vh] items-center justify-center">
    <Spinner className="size-7" label={label} />
  </div>
)
