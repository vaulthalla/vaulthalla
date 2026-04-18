'use client'

import { Slot } from '@radix-ui/react-slot'
import { cva, type VariantProps } from 'class-variance-authority'
import React from 'react'
import { cn } from '@/util/cn'

const buttonVariants = cva(
  'inline-flex items-center justify-center my-2.5 rounded-md text-lg w-full font-medium ring-offset-background '
    + 'transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 '
    + 'disabled:pointer-events-none disabled:opacity-50 transform transition-transform duration-200 hover:scale-[1.025] active:scale-100 '
    + 'cursor-pointer select-none',
  {
    variants: {
      variant: {
        default: 'bg-primary text-primary-foreground shadow-md hover:bg-primary/90',
        destructive:
          'bg-destructive text-destructive-foreground shadow-inner shadow-red-900/50 hover:bg-destructive/90',
        outline: 'border border-input bg-background text-foreground hover:bg-muted/20 hover:text-foreground',
        secondary: 'bg-secondary text-secondary-foreground shadow hover:bg-secondary/70',
        ghost: 'bg-transparent text-foreground hover:bg-muted/10 hover:text-accent-foreground',
        link: 'text-primary underline-offset-4 hover:underline hover:text-primary/90',
        glow: 'bg-primary text-primary-foreground shadow-[0_0_12px_var(--glow-cyan)] hover:shadow-[0_0_18px_var(--glow-cyan)]',
        glass:
          'bg-white/5 backdrop-blur-sm text-foreground border border-white/10 hover:bg-white/10 shadow-inner hover:shadow-md',
      },
      size: {
        default: 'h-10 px-4 py-2',
        sm: 'h-9 rounded-md px-3 text-sm',
        lg: 'h-12 rounded-lg px-6 text-xl',
        icon: 'h-10 w-10 p-0',
      },
    },
    defaultVariants: { variant: 'default', size: 'default' },
  },
)

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {
  asChild?: boolean
}

const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, asChild = false, ...props }, ref) => {
    const Comp = asChild ? Slot : 'button'
    return <Comp className={cn(buttonVariants({ variant, size, className }))} ref={ref} {...props} />
  },
)

Button.displayName = 'Button'

export { Button, buttonVariants }
