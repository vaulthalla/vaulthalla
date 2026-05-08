import Logo from '@/components/Logo'
import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { ToggleNavButton } from '@/components/nav/util'
import { FSRailActions } from '@/components/nav/fs/FSRailActions'
import { SlimRailSidebar } from '@/components/nav/SlimRailSidebar'

export const FSRail = ({ config }: { config: NavConfig }) => {
  const btnProps = { isCompact: true }

  return (
    <SlimRailSidebar
      header={
        <div className="pt-1">
          <Logo />
        </div>
      }
      controls={
        <div className="w-[85%]">
          <ToggleNavButton {...btnProps} />
        </div>
      }
      nav={
        <nav className="bg-secondary text-primary w-[85%] rounded-xl p-2 shadow-lg">
          <NavList items={config.items} compact />
        </nav>
      }
      actions={<FSRailActions />}
    />
  )
}
