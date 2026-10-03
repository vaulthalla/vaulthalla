'use client'

import React from 'react'
import * as RD from '@radix-ui/react-dialog'
import { cn } from '@/util/cn'

export const Dialog = RD.Root
export const DialogTrigger = RD.Trigger
export const DialogClose = RD.Close

interface DialogContentProps extends Omit<React.ComponentPropsWithoutRef<typeof RD.Content>, 'title'> {
  title: React.ReactNode
  description?: React.ReactNode
  size?: 'sm' | 'md' | 'lg' | 'xl'
  footer?: React.ReactNode
  hideClose?: boolean
}

const widths = { sm: 'max-w-sm', md: 'max-w-lg', lg: 'max-w-2xl', xl: 'max-w-4xl' }

export const DialogContent = React.forwardRef<React.ElementRef<typeof RD.Content>, DialogContentProps>(
  ({ title, description, size = 'md', footer, hideClose, className, children, ...props }, ref) => (
    <RD.Portal>
      <RD.Overlay className="fixed inset-0 z-[60] animate-fade-in bg-black/60 backdrop-blur-[2px]" />
      <RD.Content
        ref={ref}
        className={cn(
          'glass-strong fixed top-1/2 left-1/2 z-[61] flex max-h-[min(88dvh,820px)] w-[calc(100vw-2rem)] -translate-x-1/2 -translate-y-1/2 animate-pop-in flex-col rounded-panel focus:outline-none',
          widths[size],
          className,
        )}
        {...(description ? {} : { 'aria-describedby': undefined })}
        {...props}>
        <div className="flex items-start justify-between gap-4 px-5 pt-5">
          <div className="min-w-0">
            <RD.Title className="text-base font-semibold text-fg">{title}</RD.Title>
            {description ? <RD.Description className="mt-1 text-sm text-fg-subtle">{description}</RD.Description> : null}
          </div>
          {hideClose ? null : (
            <RD.Close
              aria-label="Close"
              className="-mt-1 -mr-1 rounded-md p-1.5 text-fg-subtle transition-colors hover:bg-surface-2 hover:text-fg">
              <svg viewBox="0 0 16 16" className="size-4" aria-hidden>
                <path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
              </svg>
            </RD.Close>
          )}
        </div>
        <div className="min-h-0 flex-1 overflow-y-auto px-5 py-4">{children}</div>
        {footer ? <div className="flex flex-wrap justify-end gap-2 border-t border-line px-5 py-3.5">{footer}</div> : null}
      </RD.Content>
    </RD.Portal>
  ),
)
DialogContent.displayName = 'DialogContent'

// A side sheet: the same dialog semantics, docked to the right edge.
export const SheetContent = React.forwardRef<
  React.ElementRef<typeof RD.Content>,
  Omit<DialogContentProps, 'size'> & { width?: string }
>(({ title, description, footer, hideClose, className, width = 'max-w-md', children, ...props }, ref) => (
  <RD.Portal>
    <RD.Overlay className="fixed inset-0 z-[60] animate-fade-in bg-black/50" />
    <RD.Content
      ref={ref}
      className={cn(
        'glass-strong fixed inset-y-2 right-2 z-[61] flex w-[calc(100vw-1rem)] animate-pop-in flex-col rounded-panel focus:outline-none',
        width,
        className,
      )}
      {...(description ? {} : { 'aria-describedby': undefined })}
      {...props}>
      <div className="flex items-start justify-between gap-4 border-b border-line px-5 py-4">
        <div className="min-w-0">
          <RD.Title className="truncate text-base font-semibold text-fg">{title}</RD.Title>
          {description ? <RD.Description className="mt-1 text-sm text-fg-subtle">{description}</RD.Description> : null}
        </div>
        {hideClose ? null : (
          <RD.Close aria-label="Close" className="rounded-md p-1.5 text-fg-subtle hover:bg-surface-2 hover:text-fg">
            <svg viewBox="0 0 16 16" className="size-4" aria-hidden>
              <path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
            </svg>
          </RD.Close>
        )}
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto px-5 py-4">{children}</div>
      {footer ? <div className="flex flex-wrap justify-end gap-2 border-t border-line px-5 py-3.5">{footer}</div> : null}
    </RD.Content>
  </RD.Portal>
))
SheetContent.displayName = 'SheetContent'
