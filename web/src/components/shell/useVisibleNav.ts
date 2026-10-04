'use client'

import { NAV, type NavSection } from '@/components/shell/nav'
import { useSession } from '@/lib/session'
import { meets } from '@/lib/permissions'

export const useVisibleNav = (): NavSection[] => {
  const user = useSession(state => state.user)
  return NAV.map(section => ({ ...section, items: section.items.filter(item => meets(user, item.requires)) })).filter(
    section => section.items.length > 0,
  )
}

