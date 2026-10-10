import React from 'react'
import { Slot } from '@radix-ui/react-slot'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from '@/util/cn'
import { Spinner } from '@/components/ui/Spinner'

export const buttonVariants = cva(
  'inline-flex shrink-0 select-none items-center justify-center gap-2 whitespace-nowrap rounded-control text-sm font-medium transition-[background-color,border-color,color,box-shadow,opacity] duration-150 disabled:pointer-events-none disabled:opacity-45 [&_svg]:size-4 [&_svg]:shrink-0',
  {
    variants: {
      variant: {
        primary: 'bg-accent text-accent-ink [&_svg]:text-inherit shadow-[0_0_0_1px_rgb(34_211_238/0.5),0_8px_24px_-10px_var(--accent-glow)] hover:bg-accent-text',
        secondary: 'border border-line-strong bg-surface-2 text-fg hover:border-accent-line hover:bg-surface-3',
        ghost: 'text-fg-muted hover:bg-surface-2 hover:text-fg',
        subtle: 'border border-accent-line bg-accent-soft text-accent-text [&_svg]:text-inherit hover:bg-accent/20',
        danger: 'border border-danger-line bg-danger-soft text-danger [&_svg]:text-inherit hover:bg-danger/20',
        'danger-solid': 'bg-danger-strong text-white [&_svg]:text-inherit hover:bg-danger-strong/85',
        link: 'h-auto px-0 text-accent-text [&_svg]:text-inherit underline-offset-4 hover:underline',
      },
      size: {
        sm: 'h-8 px-3 text-[13px]',
        md: 'h-9 px-4',
        lg: 'h-11 px-5 text-[15px]',
        icon: 'size-9 p-0',
        'icon-sm': 'size-8 p-0',
      },
    },
    defaultVariants: { variant: 'secondary', size: 'md' },
  },
)

export interface ButtonProps extends React.ButtonHTMLAttributes<HTMLButtonElement>, VariantProps<typeof buttonVariants> {
  asChild?: boolean
  loading?: boolean
}

export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, asChild = false, loading = false, disabled, children, type, ...props }, ref) => {
    const Comp = asChild ? Slot : 'button'
    return (
      <Comp
        ref={ref}
        type={asChild ? undefined : (type ?? 'button')}
        className={cn(buttonVariants({ variant, size }), className)}
        disabled={asChild ? undefined : disabled || loading}
        aria-busy={loading || undefined}
        {...props}>
        {asChild ? (
          children
        ) : (
          <>
            {loading ? <Spinner className="size-4" /> : null}
            {children}
          </>
        )}
      </Comp>
    )
  },
)
Button.displayName = 'Button'
