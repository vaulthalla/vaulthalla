'use client'

import React from 'react'
import { QueryClientProvider } from '@tanstack/react-query'
import { queryClient } from '@/lib/query'
import { TooltipProvider } from '@/components/ui/Tooltip'
import { Toaster } from '@/components/ui/Toast'
import { ConfirmHost } from '@/components/ui/Confirm'

export const Providers = ({ children }: { children: React.ReactNode }) => (
  <QueryClientProvider client={queryClient}>
    <TooltipProvider>
      {children}
      <ConfirmHost />
      <Toaster />
    </TooltipProvider>
  </QueryClientProvider>
)
