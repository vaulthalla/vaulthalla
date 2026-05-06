'use client'

import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { Banner, NavFooter, NavSpacer, ToggleNavButton } from '@/components/nav/util'
import Logo from '@/components/Logo'
import { usePathname } from 'next/navigation'

export const AdminSidebar = ({ config }: { config: NavConfig }) => {
  const pathname = usePathname()
  const isDashboardRoute = pathname === '/dashboard' || pathname.startsWith('/dashboard/')
  const btnProps = { isCompact: isDashboardRoute }

  return (
    <aside
      className={[
        'hidden h-full border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 shadow-[0_0_60px_20px_rgba(100,255,255,0.1)] backdrop-blur-xl backdrop-saturate-150 transition-[width] duration-200 md:block',
        isDashboardRoute ? 'w-20' : 'w-80',
      ].join(' ')}>
      <div className={['flex h-full flex-col space-y-3', isDashboardRoute ? 'p-3' : 'p-6'].join(' ')}>
        {isDashboardRoute ?
          <div className="flex justify-center [&_img]:mb-1 [&_img]:max-w-12">
            <Logo />
          </div>
        : <Banner />}
        <ToggleNavButton {...btnProps} />

        <nav className={['bg-secondary text-primary rounded-xl shadow-lg', isDashboardRoute ? 'p-2' : 'p-4'].join(' ')}>
          <NavList items={config.items} compact={isDashboardRoute} />
        </nav>

        <NavSpacer />
        <LogoutButton {...btnProps} />
        {!isDashboardRoute ? <NavFooter /> : null}
      </div>
    </aside>
  )
}
