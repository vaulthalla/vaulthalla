import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { Banner, NavFooter, NavSpacer, ToggleNavButton } from '@/components/nav/util'
import Logo from '@/components/Logo'
import { AdminSidebarMode, SidebarCollapseButton } from '@/components/nav/admin/AdminSidebarMode.client'

const SidebarFrame = ({ compact, config }: { compact: boolean; config: NavConfig }) => {
  const btnProps = { isCompact: compact }
  return (
    <aside
      className={[
        'hidden h-full border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 shadow-[0_0_60px_20px_rgba(100,255,255,0.1)] backdrop-blur-xl backdrop-saturate-150 transition-[width] duration-200 md:block',
        compact ? 'w-20' : 'w-80',
      ].join(' ')}>
      <div className={['flex h-full flex-col space-y-3', compact ? 'p-3' : 'p-6'].join(' ')}>
        {compact ?
          <div className="flex justify-center [&_img]:mb-1 [&_img]:max-w-12">
            <Logo />
          </div>
        : <Banner />}
        <SidebarCollapseButton compact={compact} />
        <ToggleNavButton {...btnProps} />

        <nav className={['bg-secondary text-primary rounded-xl shadow-lg', compact ? 'p-2' : 'p-4'].join(' ')}>
          <NavList items={config.items} compact={compact} />
        </nav>

        <NavSpacer />
        <LogoutButton {...btnProps} />
        {!compact ? <NavFooter /> : null}
      </div>
    </aside>
  )
}

export const AdminSidebar = ({ config }: { config: NavConfig }) => (
  <AdminSidebarMode
    compact={<SidebarFrame compact config={config} />}
    full={<SidebarFrame compact={false} config={config} />}
  />
)
