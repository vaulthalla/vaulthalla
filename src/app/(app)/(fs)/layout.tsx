import React from 'react'
import type { Metadata } from 'next'
import { Geist, Geist_Mono } from 'next/font/google'
import '@/app/globals.css'
import { WebSocketProvider } from '@/components/WebSocketProvider'
import { AuthRefresher } from '@/components/auth/AutoRefresher'
import RequireAuth from '@/components/auth/RequireAuth'
import { fsNav } from '@/config/nav/filesystem'
import { FSRail } from '@/components/nav/fs/FSRail'
import { MobileDrawer } from '@/components/nav/mobile/MobileDrawer'

const geistSans = Geist({ variable: '--font-geist-sans', subsets: ['latin'] })

const geistMono = Geist_Mono({ variable: '--font-geist-mono', subsets: ['latin'] })

export const metadata: Metadata = { title: 'Filesystem | Vaulthalla', description: 'The Final Cloud' }

const MainLayout = ({ children }: { children: React.ReactNode }) => (
  <RequireAuth>
    <div className="flex h-screen">
      <FSRail config={fsNav} />

      <div className="flex min-w-0 grow flex-col">
        <div className="border-b border-white/10 bg-black/20 p-3 backdrop-blur md:hidden">
          <MobileDrawer mode="fs" title="Files" />
        </div>

        <main className="container mx-auto my-4 min-w-0 grow">{children}</main>
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
