'use client'

import { usePathname } from 'next/navigation'
import { useCallback, useEffect, useState, type ReactNode } from 'react'

export const ADMIN_SIDEBAR_STORAGE_KEY = 'vaulthalla.admin.sidebar.compact.v1'
export const ADMIN_SIDEBAR_EVENT = 'vaulthalla:admin-sidebar-mode'

function storedCompactPreference(): boolean | null {
  if (typeof window === 'undefined') return null
  const raw = window.localStorage.getItem(ADMIN_SIDEBAR_STORAGE_KEY)
  if (raw === 'compact') return true
  if (raw === 'full') return false
  return null
}

export const AdminSidebarMode = ({
  compact,
  full,
}: {
  compact: ReactNode
  full: ReactNode
}) => {
  const pathname = usePathname()
  const isDashboardRoute = pathname === '/dashboard' || pathname.startsWith('/dashboard/')
  const [preference, setPreference] = useState<boolean | null>(null)

  useEffect(() => {
    setPreference(storedCompactPreference())

    const update = () => setPreference(storedCompactPreference())
    window.addEventListener(ADMIN_SIDEBAR_EVENT, update)
    window.addEventListener('storage', update)
    return () => {
      window.removeEventListener(ADMIN_SIDEBAR_EVENT, update)
      window.removeEventListener('storage', update)
    }
  }, [])

  const effectiveCompact = preference ?? isDashboardRoute

  return effectiveCompact ? compact : full
}

export const SidebarCollapseButton = ({ compact }: { compact: boolean }) => {
  const nextCompact = !compact
  const label = compact ? 'Expand sidebar' : 'Collapse sidebar'
  const handleClick = useCallback(() => {
    window.localStorage.setItem(ADMIN_SIDEBAR_STORAGE_KEY, nextCompact ? 'compact' : 'full')
    window.dispatchEvent(new Event(ADMIN_SIDEBAR_EVENT))
  }, [nextCompact])

  return (
    <button
      type="button"
      className={[
        'inline-flex items-center justify-center rounded-xl border border-white/10 bg-white/5 text-cyan-100/75 transition hover:border-cyan-200/35 hover:bg-cyan-400/10 hover:text-cyan-50',
        compact ? 'h-9 w-full' : 'px-3 py-2 text-xs',
      ].join(' ')}
      aria-label={label}
      title={label}
      onClick={handleClick}>
      {compact ? '›' : '‹ Collapse'}
    </button>
  )
}
