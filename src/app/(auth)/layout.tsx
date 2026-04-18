import React from 'react'
import type { Metadata } from 'next'

export const metadata: Metadata = { title: 'Login | Vaulthalla', description: 'The Final Cloud' }

export default function Layout({ children }: { children: React.ReactNode }) {
  return (
    <div className="flex min-h-screen w-full text-white">
      <main className="flex flex-1 justify-center overflow-y-auto">{children}</main>
    </div>
  )
}
