'use client'

import React, { useEffect, useState } from 'react'
import dynamic from 'next/dynamic'
import { QueryClientProvider } from '@tanstack/react-query'
import { queryClient } from '@/lib/query'
import { TooltipProvider } from '@/components/ui/Tooltip'

// Overlay hosts aren't needed for first paint: load them once the page is idle.
const ConfirmHost = dynamic(() => import('@/components/ui/ConfirmHost').then(m => m.ConfirmHost), { ssr: false })
const Toaster = dynamic(() => import('@/components/ui/ToasterHost').then(m => m.Toaster), { ssr: false })

const useIdle = () => {
  const [idle, setIdle] = useState(false)
  useEffect(() => {
    const ric = window.requestIdleCallback ?? ((cb: () => void) => setTimeout(cb, 200))
    ric(() => setIdle(true))
  }, [])
  return idle
}

export const Providers = ({ children }: { children: React.ReactNode }) => {
  const idle = useIdle()
  return (
    <QueryClientProvider client={queryClient}>
      <TooltipProvider>
        {children}
        {idle ? (
          <>
            <ConfirmHost />
            <Toaster />
          </>
        ) : null}
      </TooltipProvider>
    </QueryClientProvider>
  )
}
