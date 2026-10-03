import React from 'react'
import type { Metadata } from 'next'
import { VaultShell } from '@/features/vaults/VaultShell'

export const metadata: Metadata = { title: 'Vault' }

export default function Layout({ children }: { children: React.ReactNode }) {
  return <VaultShell>{children}</VaultShell>
}
