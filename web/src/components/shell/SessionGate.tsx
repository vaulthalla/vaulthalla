'use client'

import React, { useEffect } from 'react'
import { usePathname } from 'next/navigation'
import { api, endSession, refreshSession, useSession } from '@/lib/session'
import { Spinner } from '@/components/ui/Spinner'

// Re-establishes the session from the HttpOnly refresh cookie (the access token is never persisted), keeps it fresh,
// and sends the visitor to login when the server refuses it.
export const SessionGate = ({ children }: { children: React.ReactNode }) => {
  const status = useSession(state => state.status)
  const pathname = usePathname()

  useEffect(() => {
    api.connect()
    if (useSession.getState().status === 'unknown') void refreshSession()
  }, [])

  useEffect(() => {
    if (status === 'unauthenticated') endSession(pathname ?? undefined)
  }, [status, pathname])

  // Access tokens live ~60 minutes; refresh well before that, and right after the tab wakes up.
  useEffect(() => {
    if (status !== 'authenticated') return
    const interval = setInterval(() => void refreshSession(), 10 * 60_000)
    const onVisible = () => {
      if (document.visibilityState === 'visible') void refreshSession()
    }
    document.addEventListener('visibilitychange', onVisible)
    return () => {
      clearInterval(interval)
      document.removeEventListener('visibilitychange', onVisible)
    }
  }, [status])

  if (status !== 'authenticated')
    return (
      <div className="grid min-h-dvh place-items-center">
        <Spinner className="size-7" label="Signing in" />
      </div>
    )
  return <>{children}</>
}
