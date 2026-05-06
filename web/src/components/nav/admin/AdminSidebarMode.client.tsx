'use client'

import { usePathname } from 'next/navigation'
import type { ReactNode } from 'react'

export const AdminSidebarMode = ({
  compact,
  full,
}: {
  compact: ReactNode
  full: ReactNode
}) => {
  const pathname = usePathname()
  const isDashboardRoute = pathname === '/dashboard' || pathname.startsWith('/dashboard/')

  return isDashboardRoute ? compact : full
}
