import React from 'react'
import type { Metadata } from 'next'
import { Geist, Geist_Mono } from 'next/font/google'
import '@/app/globals.css'
import { WebSocketProvider } from '@/components/WebSocketProvider'
import { AuthRefresher } from '@/components/auth/AutoRefresher'
import RequireAuth from '@/components/auth/RequireAuth'
import { adminNav } from '@/config/nav/admin'
import { AdminSidebar } from '@/components/nav/admin/AdminSidebar'
import { MobileDrawer } from '@/components/nav/mobile/MobileDrawer'

const geistSans = Geist({ variable: '--font-geist-sans', subsets: ['latin'] })

const geistMono = Geist_Mono({ variable: '--font-geist-mono', subsets: ['latin'] })

export const metadata: Metadata = { title: 'Vaulthalla', description: 'The Final Cloud' }

const MainLayout = ({ children }: { children: React.ReactNode }) => (
  <RequireAuth>
    <div className="flex h-screen">
      <AdminSidebar config={adminNav} />

      <div className="flex min-w-0 grow flex-col">
        <div className="border-b border-white/10 bg-black/20 p-3 backdrop-blur md:hidden">
          <MobileDrawer mode="admin" title="Admin" />
        </div>

        <main className="min-w-0 grow p-6 md:p-10">{children}</main>
      </div>
    </div>
  </RequireAuth>
)

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body className={`${geistSans.variable} ${geistMono.variable} bg-background antialiased`}>
        <AuthRefresher />
        <WebSocketProvider />
        <MainLayout>{children}</MainLayout>
      </body>
    </html>
  )
}
