import React from 'react'
import type { Metadata } from 'next'
import { AuthRefresher } from '@/components/auth/AutoRefresher'
import RequireAuth from '@/components/auth/RequireAuth'
import { adminNav } from '@/config/nav/admin'
import { AdminSidebar } from '@/components/nav/admin/AdminSidebar'
import { MobileDrawer } from '@/components/nav/mobile/MobileDrawer'

export const metadata: Metadata = { title: 'Vaulthalla', description: 'The Final Cloud' }

export default function Layout({ children }: { children: React.ReactNode }) {
  return (
    <>
      <AuthRefresher />
      <RequireAuth>
        <div className="flex h-screen">
          <AdminSidebar config={adminNav} />

          <div className="flex max-h-screen min-w-0 grow flex-col overflow-scroll">
            <div className="border-b border-white/10 bg-black/20 p-3 backdrop-blur md:hidden">
              <MobileDrawer mode="admin" title="Admin" />
            </div>

            <main className="min-w-0 grow p-6 lg:p-8">{children}</main>
          </div>
        </div>
      </RequireAuth>
    </>
  )
}
