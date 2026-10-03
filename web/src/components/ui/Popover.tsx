'use client'

import React, { Suspense, lazy } from 'react'

// Popover whose Radix implementation loads the first time it opens.
const Impl = lazy(() => import('@/components/ui/PopoverImpl'))

export const Popover = ({
  open,
  onOpenChange,
  trigger,
  children,
  className,
  align = 'end',
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  trigger: React.ReactElement<React.HTMLAttributes<HTMLElement>>
  children: React.ReactNode
  className?: string
  align?: 'start' | 'center' | 'end'
}) => {
  const [armed, setArmed] = React.useState(open)
  if (open && !armed) setArmed(true)
  if (!armed) return React.cloneElement(trigger, { onClick: () => onOpenChange(true), onPointerEnter: () => void import('@/components/ui/PopoverImpl') })
  return (
    <Suspense fallback={trigger}>
      <Impl open={open} onOpenChange={onOpenChange} trigger={trigger} className={className} align={align}>
        {children}
      </Impl>
    </Suspense>
  )
}
