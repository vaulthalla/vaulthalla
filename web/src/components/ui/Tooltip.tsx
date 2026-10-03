'use client'

import React from 'react'
import * as RT from '@radix-ui/react-tooltip'
import { cn } from '@/util/cn'

export const TooltipProvider = ({ children }: { children: React.ReactNode }) => (
  <RT.Provider delayDuration={350} skipDelayDuration={150}>
    {children}
  </RT.Provider>
)

export const Tooltip = ({
  content,
  children,
  side = 'top',
  className,
}: {
  content: React.ReactNode
  children: React.ReactNode
  side?: 'top' | 'right' | 'bottom' | 'left'
  className?: string
}) => {
  if (!content) return <>{children}</>
  return (
    <RT.Root>
      <RT.Trigger asChild>{children}</RT.Trigger>
      <RT.Portal>
        <RT.Content
          side={side}
          sideOffset={6}
          collisionPadding={8}
          className={cn(
            'glass-strong z-[70] max-w-xs animate-fade-in rounded-md px-2 py-1 text-xs leading-snug text-fg',
            className,
          )}>
          {content}
        </RT.Content>
      </RT.Portal>
    </RT.Root>
  )
}
