'use client'

import React, { useEffect } from 'react'
import { usePathname } from 'next/navigation'
import * as RD from '@radix-ui/react-dialog'
import { useVisibleNav } from '@/components/shell/useVisibleNav'
import { Brand, NavSections } from '@/components/shell/AppShell'

export const MobileNavSheet = ({ open, onOpenChange }: { open: boolean; onOpenChange: (open: boolean) => void }) => {
  const sections = useVisibleNav()
  const pathname = usePathname()
  useEffect(() => onOpenChange(false), [pathname, onOpenChange])
  return (
    <RD.Root open={open} onOpenChange={onOpenChange}>
      <RD.Portal>
        <RD.Overlay className="fixed inset-0 z-[60] animate-fade-in bg-black/60" />
        <RD.Content
          aria-describedby={undefined}
          className="glass-strong fixed inset-y-0 left-0 z-[61] flex w-72 max-w-[85vw] animate-pop-in flex-col rounded-r-panel focus:outline-none">
          <RD.Title className="sr-only">Navigation</RD.Title>
          <div className="flex h-14 items-center px-4">
            <Brand collapsed={false} />
          </div>
          <div className="min-h-0 flex-1 overflow-y-auto px-3 py-3">
            <NavSections sections={sections} collapsed={false} onNavigate={() => onOpenChange(false)} />
          </div>
          <div className="border-t border-line px-4 py-3 font-mono text-[11px] text-fg-faint">v{process.env.NEXT_PUBLIC_VAULTHALLA_VERSION}</div>
        </RD.Content>
      </RD.Portal>
    </RD.Root>
  )
}
