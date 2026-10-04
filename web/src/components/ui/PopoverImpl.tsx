'use client'

import React from 'react'
import * as RP from '@radix-ui/react-popover'
import { cn } from '@/util/cn'

export default function PopoverImpl({
  open,
  onOpenChange,
  trigger,
  children,
  className,
  align,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  trigger: React.ReactElement
  children: React.ReactNode
  className?: string
  align: 'start' | 'center' | 'end'
}) {
  return (
    <RP.Root open={open} onOpenChange={onOpenChange}>
      <RP.Trigger asChild>{trigger}</RP.Trigger>
      <RP.Portal>
        <RP.Content align={align} sideOffset={8} collisionPadding={8} className={cn('glass-strong z-[65] animate-pop-in rounded-panel', className)}>
          {children}
        </RP.Content>
      </RP.Portal>
    </RP.Root>
  )
}
