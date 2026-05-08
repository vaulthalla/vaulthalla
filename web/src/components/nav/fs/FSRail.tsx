import Logo from '@/components/Logo'
import { NavList } from '@/components/nav/NavList'
import type { NavConfig } from '@/components/nav/types'
import { ToggleNavButton } from '@/components/nav/util'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { FSRailActions } from '@/components/nav/fs/FSRailActions'

export const FSRail = ({ config }: { config: NavConfig }) => {
  const btnProps = { isCompact: true }

  return (
    <aside className="hidden h-screen w-20 shrink-0 border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 backdrop-blur-xl backdrop-saturate-150 md:block">
      <div className="flex h-full min-h-0 flex-col items-center p-2">
        <div className="flex w-full flex-col items-center gap-3">
          <div className="pt-1">
            <Logo />
          </div>

          <div className="w-[85%]">
            <ToggleNavButton {...btnProps} />
          </div>

          <nav className="bg-secondary text-primary w-[85%] rounded-xl p-2 shadow-lg">
            <NavList items={config.items} compact />
          </nav>

          <FSRailActions />
        </div>

        <div className="mt-auto flex w-full justify-center pb-1">
          <LogoutButton isCompact={true} />
        </div>
      </div>
    </aside>
  )
}
