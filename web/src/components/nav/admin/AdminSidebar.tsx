import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { Banner, NavFooter, NavSpacer, ToggleNavButton } from '@/components/nav/util'
import Logo from '@/components/Logo'
import { AdminSidebarMode, SidebarCollapseButton } from '@/components/nav/admin/AdminSidebarMode.client'
import { SlimRailSidebar } from '@/components/nav/SlimRailSidebar'

const CompactSidebarFrame = ({ config }: { config: NavConfig }) => {
  const btnProps = { isCompact: true }

  return (
    <SlimRailSidebar
      className="shadow-[0_0_60px_20px_rgba(100,255,255,0.1)] transition-[width] duration-200"
      header={
        <div className="flex justify-center pt-1 [&_img]:mb-1 [&_img]:max-w-12">
          <Logo />
        </div>
      }
      controls={
        <>
          <div className="w-[85%]">
            <SidebarCollapseButton compact />
          </div>
          <div className="w-[85%]">
            <ToggleNavButton {...btnProps} />
          </div>
        </>
      }
      nav={
        <nav className="bg-secondary text-primary w-[85%] rounded-xl p-2 shadow-lg">
          <NavList items={config.items} compact />
        </nav>
      }
    />
  )
}

const FullSidebarFrame = ({ config }: { config: NavConfig }) => {
  const btnProps = { isCompact: false }

  return (
    <aside
      className="hidden h-full w-80 border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 shadow-[0_0_60px_20px_rgba(100,255,255,0.1)] backdrop-blur-xl backdrop-saturate-150 transition-[width] duration-200 md:block">
      <div className="flex h-full flex-col space-y-3 p-6">
        <Banner />
        <SidebarCollapseButton compact={false} />
        <ToggleNavButton {...btnProps} />

        <nav className="bg-secondary text-primary rounded-xl p-4 shadow-lg">
          <NavList items={config.items} />
        </nav>

        <NavSpacer />
        <LogoutButton {...btnProps} />
        <NavFooter />
      </div>
    </aside>
  )
}

export const AdminSidebar = ({ config }: { config: NavConfig }) => (
  <AdminSidebarMode
    compact={<CompactSidebarFrame config={config} />}
    full={<FullSidebarFrame config={config} />}
  />
)
