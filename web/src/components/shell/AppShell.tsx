'use client'

import React, { useEffect, useState } from 'react'
import Link from 'next/link'
import NextImage from 'next/image'
import { usePathname } from 'next/navigation'
import * as RD from '@radix-ui/react-dialog'
import { cn } from '@/util/cn'
import { NAV, type NavItem, type NavSection } from '@/components/shell/nav'
import { useUiPrefs } from '@/components/shell/uiPrefs'
import { useSession } from '@/lib/session'
import { meets } from '@/lib/permissions'
import { Tooltip } from '@/components/ui/Tooltip'
import { IconButton } from '@/components/ui/IconButton'
import { BarsIcon, ChevronLeftIcon, MagnifyingGlassIcon } from '@/components/ui/icons'
import { Kbd } from '@/components/ui/Badge'
import { CommandPalette, useCommandPalette } from '@/components/shell/CommandPalette'
import { UserMenu } from '@/components/shell/UserMenu'
import { ConnectionIndicator } from '@/components/shell/ConnectionIndicator'
import { TopBarExtras } from '@/components/shell/TopBarExtras'
import Logo from '@/public/vaulthalla-logo.png'
import pkg from '../../../package.json'

export const useVisibleNav = (): NavSection[] => {
  const user = useSession(state => state.user)
  return NAV.map(section => ({ ...section, items: section.items.filter(item => meets(user, item.requires)) })).filter(
    section => section.items.length > 0,
  )
}

const isActive = (pathname: string, href: string) => pathname === href || pathname.startsWith(`${href}/`)

const NavLink = ({ item, collapsed, onNavigate }: { item: NavItem; collapsed: boolean; onNavigate?: () => void }) => {
  const pathname = usePathname() ?? ''
  const active = isActive(pathname, item.href)
  const link = (
    <Link
      href={item.href}
      onClick={onNavigate}
      aria-current={active ? 'page' : undefined}
      className={cn(
        'group relative flex h-9 items-center gap-3 rounded-control px-2.5 text-sm text-fg-muted transition-colors hover:bg-surface-2 hover:text-fg',
        active && 'bg-accent-soft text-fg shadow-[inset_0_0_0_1px_var(--accent-line)]',
        collapsed && 'justify-center px-0',
      )}>
      {active ? <span aria-hidden className="absolute top-2 bottom-2 -left-2 w-[3px] rounded-full bg-accent shadow-[0_0_10px_var(--accent-glow)]" /> : null}
      <item.icon aria-hidden className={cn('size-[18px] shrink-0 text-fg-subtle transition-colors group-hover:text-fg-muted', active && 'text-accent-text group-hover:text-accent-text')} />
      {collapsed ? <span className="sr-only">{item.label}</span> : <span className="truncate">{item.label}</span>}
    </Link>
  )
  return collapsed ? (
    <Tooltip content={item.label} side="right">
      {link}
    </Tooltip>
  ) : (
    link
  )
}

const NavSections = ({ sections, collapsed, onNavigate }: { sections: NavSection[]; collapsed: boolean; onNavigate?: () => void }) => (
  <nav aria-label="Main" className="flex flex-col gap-5">
    {sections.map((section, index) => (
      <div key={section.label ?? index} className="flex flex-col gap-0.5">
        {section.label ? (
          collapsed ? (
            <div aria-hidden className="mx-auto mb-1.5 h-px w-6 bg-line" />
          ) : (
            <div className="mb-1 px-2.5 text-[11px] font-medium tracking-wider text-fg-faint uppercase">{section.label}</div>
          )
        ) : null}
        {section.items.map(item => (
          <NavLink key={item.href} item={item} collapsed={collapsed} onNavigate={onNavigate} />
        ))}
      </div>
    ))}
  </nav>
)

const Brand = ({ collapsed }: { collapsed: boolean }) => (
  <Link href="/files" className={cn('flex items-center gap-2.5 rounded-control px-1.5 py-1', collapsed && 'justify-center px-0')}>
    <NextImage src={Logo} alt="" width={30} height={30} priority className="size-[30px] drop-shadow-[0_0_10px_rgb(34_211_238/0.25)]" />
    {collapsed ? <span className="sr-only">Vaulthalla</span> : <span className="text-[15px] font-semibold tracking-tight text-fg">Vaulthalla</span>}
  </Link>
)

const Rail = () => {
  const sections = useVisibleNav()
  const collapsed = useUiPrefs(state => state.railCollapsed)
  const setCollapsed = useUiPrefs(state => state.setRailCollapsed)
  return (
    <aside
      className={cn(
        'glass sticky top-0 hidden h-dvh shrink-0 flex-col border-y-0 border-l-0 transition-[width] duration-200 md:flex',
        collapsed ? 'w-[68px]' : 'w-60',
      )}>
      <div className={cn('flex h-14 items-center px-3', collapsed && 'justify-center px-0')}>
        <Brand collapsed={collapsed} />
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto px-3 py-3 scrollbar-thin">
        <NavSections sections={sections} collapsed={collapsed} />
      </div>
      <div className={cn('flex items-center justify-between border-t border-line px-3 py-2.5', collapsed && 'flex-col gap-2 px-0')}>
        <span className="font-mono text-[11px] text-fg-faint" title="Web console version">
          v{pkg.version}
        </span>
        <IconButton
          label={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
          icon={ChevronLeftIcon}
          size="icon-sm"
          className={cn(collapsed && 'rotate-180')}
          onClick={() => setCollapsed(!collapsed)}
        />
      </div>
    </aside>
  )
}

const MobileNav = () => {
  const sections = useVisibleNav()
  const [open, setOpen] = useState(false)
  const pathname = usePathname()
  useEffect(() => setOpen(false), [pathname])
  return (
    <RD.Root open={open} onOpenChange={setOpen}>
      <RD.Trigger asChild>
        <IconButton label="Open navigation" icon={BarsIcon} className="md:hidden" tooltip={false} />
      </RD.Trigger>
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
            <NavSections sections={sections} collapsed={false} onNavigate={() => setOpen(false)} />
          </div>
          <div className="border-t border-line px-4 py-3 font-mono text-[11px] text-fg-faint">v{pkg.version}</div>
        </RD.Content>
      </RD.Portal>
    </RD.Root>
  )
}

const SearchTrigger = () => {
  const open = useCommandPalette(state => state.setOpen)
  return (
    <button
      type="button"
      onClick={() => open(true)}
      className="flex h-9 w-full max-w-md items-center gap-2.5 rounded-control border border-line bg-surface-1 px-3 text-sm text-fg-faint transition-colors hover:border-line-strong hover:text-fg-subtle">
      <MagnifyingGlassIcon className="size-4" aria-hidden />
      <span className="flex-1 truncate text-left">Search or jump to…</span>
      <span className="hidden items-center gap-1 sm:flex">
        <Kbd>⌘</Kbd>
        <Kbd>K</Kbd>
      </span>
    </button>
  )
}

const TopBar = () => (
  <header className="sticky top-0 z-40 flex h-14 items-center gap-3 border-b border-line bg-bg/70 px-3 backdrop-blur-xl sm:px-5">
    <MobileNav />
    <div className="min-w-0 flex-1">
      <SearchTrigger />
    </div>
    <div className="flex items-center gap-1.5">
      <ConnectionIndicator />
      <TopBarExtras />
      <UserMenu />
    </div>
  </header>
)

export const AppShell = ({ children }: { children: React.ReactNode }) => (
  <div className="flex min-h-dvh">
    <Rail />
    <div className="flex min-w-0 flex-1 flex-col">
      <TopBar />
      <main id="main" className="min-w-0 flex-1 px-4 py-6 sm:px-6 lg:px-8">
        <div className="mx-auto w-full max-w-[1400px]">{children}</div>
      </main>
    </div>
    <CommandPalette />
  </div>
)
