import React from 'react'
import { HealthShell } from '@/features/health/HealthShell'

export default function Layout({ children }: { children: React.ReactNode }) {
  return <HealthShell>{children}</HealthShell>
}
