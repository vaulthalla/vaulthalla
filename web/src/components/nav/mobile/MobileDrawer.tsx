'use client'

import { useState, useEffect } from 'react'
import Logo from '@/components/Logo'
import { NavList } from '@/components/nav/NavList'
import Bars from '@/fa-light/bars.svg'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { NAV } from '@/config/nav/registry'

export const MobileDrawer = ({ mode, title = 'Menu' }: { mode: 'admin' | 'fs'; title?: string }) => {
  const [open, setOpen] = useState(false)
  const config = NAV[mode]

  // optional: lock scroll when open
  useEffect(() => {
    if (!open) return
    const prev = document.body.style.overflow
    document.body.style.overflow = 'hidden'
    return () => {
      document.body.style.overflow = prev
    }
  }, [open])

  return (
    <>
      <button
        type="button"
        className="inline-flex items-center gap-2 rounded-md border border-white/10 bg-white/5 px-3 py-2 text-cyan-200 shadow-sm backdrop-blur hover:bg-white/10"
        aria-label="Open menu"
        onClick={() => setOpen(true)}>
        <Bars className="h-5 w-5 fill-current" />
        <span className="text-sm font-medium">{title}</span>
      </button>

      {open && (
        <div className="fixed inset-0 z-50 md:hidden">
          <div className="absolute inset-0 bg-black/70" onClick={() => setOpen(false)} />

          <aside className="absolute top-0 left-0 h-full w-80 max-w-[85vw] border-r border-white/20 bg-linear-to-b from-white/10 to-black/40 backdrop-blur-xl">
            <div className="flex h-full flex-col p-4">
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-3">
                  <Logo />
                  <div className="text-cyan-200">{title}</div>
                </div>
                <button
                  type="button"
                  className="rounded-md border border-white/10 bg-white/5 px-3 py-2 text-cyan-200 hover:bg-white/10"
                  aria-label="Close menu"
                  onClick={() => setOpen(false)}>
                  ✕
                </button>
              </div>

              <div className="mt-4 rounded-xl bg-white/5 p-3 shadow-lg">
                {/* Close on navigation: simplest way is to wrap links yourself; here we just close on any click within nav */}
                <div onClick={() => setOpen(false)}>
                  <NavList items={config.items} />
                </div>
              </div>

              <div className="grow" />

              <div onClick={() => setOpen(false)}>
                <LogoutButton isCompact={false} />
              </div>

              <div className="mt-3 text-xs text-cyan-400 opacity-70">v0.1.0 • Vaulthalla</div>
            </div>
          </aside>
        </div>
      )}
    </>
  )
}
