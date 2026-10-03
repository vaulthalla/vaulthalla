'use client'

import { Toaster as Sonner } from 'sonner'

export const Toaster = () => (
  <Sonner
    theme="dark"
    position="bottom-right"
    gap={8}
    toastOptions={{
      classNames: {
        toast: 'glass-strong !rounded-card !border-line-strong !bg-[rgb(10_15_22/0.92)] !text-fg !shadow-[var(--shadow-pop)]',
        description: '!text-fg-subtle',
        actionButton: '!bg-accent !text-accent-ink',
        error: '!border-danger-line',
        success: '!border-ok-line',
      },
    }}
  />
)
