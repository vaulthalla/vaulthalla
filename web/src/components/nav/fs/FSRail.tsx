import Logo from '@/components/Logo'
import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { NavSpacer, ToggleNavButton } from '@/components/nav/util'
import { LogoutButton } from '@/components/nav/LogoutButton'

export const FSRail = ({ config }: { config: NavConfig }) => {
  const btnProps = { isCompact: true }

  return (
    <aside className="hidden h-full w-20 border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 backdrop-blur-xl backdrop-saturate-150 md:block">
      <div className="flex h-full flex-col items-center space-y-3 p-2">
        <div className="pt-1">
          <Logo />
        </div>

        <ToggleNavButton {...btnProps} />

        <nav className="bg-secondary text-primary w-[85%] rounded-xl p-2 shadow-lg">
          <NavList items={config.items} compact />
        </nav>

        <NavSpacer />
        <LogoutButton isCompact={true} />
      </div>
    </aside>
  )
}
