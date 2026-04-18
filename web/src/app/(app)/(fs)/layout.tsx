import React from 'react'
import type { Metadata } from 'next'
import { AuthRefresher } from '@/components/auth/AutoRefresher'
import RequireAuth from '@/components/auth/RequireAuth'
import { fsNav } from '@/config/nav/filesystem'
import { FSRail } from '@/components/nav/fs/FSRail'
import { MobileDrawer } from '@/components/nav/mobile/MobileDrawer'

export const metadata: Metadata = { title: 'Filesystem | Vaulthalla', description: 'The Final Cloud' }

export default function Layout({ children }: { children: React.ReactNode }) {
  return (
    <>
      <AuthRefresher />
      <RequireAuth>
        <div className="flex min-h-screen">
          <FSRail config={fsNav} />
          <div className="flex min-w-0 grow flex-col">
            <div className="border-b border-white/10 bg-black/20 p-3 backdrop-blur md:hidden">
              <MobileDrawer mode="fs" title="Files" />
            </div>

            <main className="min-w-0 grow overflow-y-auto px-6 py-4 lg:px-8">{children}</main>
          </div>
        </div>
      </RequireAuth>
    </>
  )
}
