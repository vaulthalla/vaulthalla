'use client'

import { Toaster as Sonner, toast as sonner } from 'sonner'
import { errorMessage } from '@/lib/ws/errors'

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

// One notification channel for every mutation result.
export const notify = {
  success: (message: string, description?: string) => sonner.success(message, { description }),
  info: (message: string, description?: string) => sonner(message, { description }),
  error: (error: unknown, fallback = 'Something went wrong') =>
    sonner.error(errorMessage(error, fallback), { duration: 8000 }),
}
