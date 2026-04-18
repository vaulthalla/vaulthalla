import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { Banner, NavFooter, NavSpacer, ToggleNavButton } from '@/components/nav/util'

export const AdminSidebar = async ({ config }: { config: NavConfig }) => {
  const btnProps = { isCompact: false }

  return (
    <aside className="hidden h-full w-80 border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 shadow-[0_0_60px_20px_rgba(100,255,255,0.1)] backdrop-blur-xl backdrop-saturate-150 md:block">
      <div className="flex h-full flex-col space-y-3 p-6">
        <Banner />
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
