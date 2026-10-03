import React from 'react'
import type { Metadata } from 'next'

export const metadata: Metadata = { title: 'Sign in' }

export default function Layout({ children }: { children: React.ReactNode }) {
  return <main className="grid min-h-dvh place-items-center px-4 py-10">{children}</main>
}
