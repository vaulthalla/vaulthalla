import React from 'react'
import { cn } from '@/util/cn'

// CSS-only tooltip: no JS positioning, nothing to load. Shows on hover and keyboard focus after a short delay.
// Use it for short labels on icon controls; anything longer belongs in visible text.
export const TooltipProvider = ({ children }: { children: React.ReactNode }) => <>{children}</>

const sides = {
  top: 'bottom-full left-1/2 mb-1.5 -translate-x-1/2',
  bottom: 'top-full left-1/2 mt-1.5 -translate-x-1/2',
  right: 'left-full top-1/2 ml-2 -translate-y-1/2',
  left: 'right-full top-1/2 mr-2 -translate-y-1/2',
}

export const Tooltip = ({
  content,
  children,
  side = 'top',
  className,
  wrapperClassName,
}: {
  content: React.ReactNode
  children: React.ReactNode
  side?: 'top' | 'right' | 'bottom' | 'left'
  className?: string
  wrapperClassName?: string
}) => {
  if (!content) return <>{children}</>
  return (
    <span className={cn('group/tip relative inline-flex', wrapperClassName)}>
      {children}
      <span
        role="tooltip"
        className={cn(
          'glass-strong pointer-events-none absolute z-[70] w-max max-w-xs rounded-md px-2 py-1 text-xs leading-snug whitespace-nowrap text-fg opacity-0 transition-opacity duration-150 group-hover/tip:opacity-100 group-hover/tip:delay-300 group-has-[:focus-visible]/tip:opacity-100',
          sides[side],
          className,
        )}>
        {content}
      </span>
    </span>
  )
}
