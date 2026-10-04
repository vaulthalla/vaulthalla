'use client'

import React from 'react'
import { Button, type ButtonProps } from '@/components/ui/Button'
import { Tooltip } from '@/components/ui/Tooltip'
import { cn } from '@/util/cn'

export interface IconButtonProps extends Omit<ButtonProps, 'size' | 'children'> {
  label: string
  icon: React.ComponentType<React.SVGProps<SVGSVGElement>>
  size?: 'icon' | 'icon-sm'
  tooltip?: boolean
}

export const IconButton = React.forwardRef<HTMLButtonElement, IconButtonProps>(
  ({ label, icon: Icon, size = 'icon', variant = 'ghost', tooltip = true, className, ...props }, ref) => {
    const button = (
      <Button ref={ref} variant={variant} size={size} aria-label={label} className={cn(className)} {...props}>
        <Icon aria-hidden />
      </Button>
    )
    return tooltip ? <Tooltip content={label}>{button}</Tooltip> : button
  },
)
IconButton.displayName = 'IconButton'
