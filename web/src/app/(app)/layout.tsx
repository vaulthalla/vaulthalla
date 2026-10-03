import React from 'react'
import { SessionGate } from '@/components/shell/SessionGate'
import { AppShell } from '@/components/shell/AppShell'
import { Providers } from '@/components/Providers'

export default function Layout({ children }: { children: React.ReactNode }) {
  return (
    <Providers>
      <SessionGate>
        <AppShell>{children}</AppShell>
      </SessionGate>
    </Providers>
  )
}
