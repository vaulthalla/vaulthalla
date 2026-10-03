'use client'

import React from 'react'
import { cn } from '@/util/cn'
import { Button } from '@/components/ui/Button'
import { Spinner } from '@/components/ui/Spinner'
import { errorMessage, isWsError } from '@/lib/ws/errors'

export const EmptyState = ({
  icon: Icon,
  title,
  description,
  action,
  className,
}: {
  icon?: React.ComponentType<React.SVGProps<SVGSVGElement>>
  title: React.ReactNode
  description?: React.ReactNode
  action?: React.ReactNode
  className?: string
}) => (
  <div className={cn('flex flex-col items-center justify-center px-6 py-14 text-center', className)}>
    {Icon ? (
      <div className="mb-4 grid size-12 place-items-center rounded-2xl border border-line bg-surface-2 text-accent-text">
        <Icon className="size-5" aria-hidden />
      </div>
    ) : null}
    <p className="text-[15px] font-medium text-fg">{title}</p>
    {description ? <p className="mt-1 max-w-md text-sm text-fg-subtle">{description}</p> : null}
    {action ? <div className="mt-5">{action}</div> : null}
  </div>
)

// Renders a failed load. Refusals get a "no access" state instead of an error, and never a spinner.
export const ErrorState = ({ error, onRetry, className }: { error: unknown; onRetry?: () => void; className?: string }) => {
  if (isWsError(error, 'denied'))
    return (
      <EmptyState
        className={className}
        title="You don't have access to this"
        description="Your role doesn't include the permission this page needs. Ask an administrator if you think it should."
      />
    )
  if (isWsError(error, 'not_found'))
    return <EmptyState className={className} title="Not found" description={errorMessage(error)} />
  return (
    <EmptyState
      className={className}
      title={isWsError(error, 'disconnected', 'timeout') ? 'Cannot reach the server' : 'This failed to load'}
      description={errorMessage(error)}
      action={onRetry ? <Button onClick={onRetry}>Try again</Button> : undefined}
    />
  )
}

interface QueryLike<T> {
  data: T | undefined
  error: unknown
  isPending: boolean
  refetch: () => unknown
}

// The one way a page renders async data: pending → skeleton/spinner, error → typed error state, data → children.
export function QueryState<T>({
  query,
  children,
  pending,
  className,
}: {
  query: QueryLike<T>
  children: (data: T) => React.ReactNode
  pending?: React.ReactNode
  className?: string
}) {
  if (query.isPending)
    return (
      <>
        {pending ?? (
          <div className={cn('flex justify-center py-14', className)}>
            <Spinner className="size-6" label="Loading" />
          </div>
        )}
      </>
    )
  if (query.error || query.data === undefined)
    return <ErrorState className={className} error={query.error} onRetry={() => void query.refetch()} />
  return <>{children(query.data)}</>
}

export const Skeleton = ({ className }: { className?: string }) => <div className={cn('skeleton h-4', className)} aria-hidden />

export const InlineError = ({ error, className }: { error: unknown; className?: string }) =>
  error ? (
    <p role="alert" className={cn('rounded-control border border-danger-line bg-danger-soft px-3 py-2 text-sm text-danger', className)}>
      {errorMessage(error)}
    </p>
  ) : null
