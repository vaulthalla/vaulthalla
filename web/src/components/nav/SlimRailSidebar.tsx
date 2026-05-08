import type { ReactNode } from 'react'
import { LogoutButton } from '@/components/nav/LogoutButton'
import { cn } from '@/util/cn'

interface SlimRailSidebarProps {
  header: ReactNode
  controls?: ReactNode
  nav: ReactNode
  actions?: ReactNode
  bottom?: ReactNode
  className?: string
  contentClassName?: string
  topClassName?: string
}

export const SlimRailSidebar = ({
  header,
  controls,
  nav,
  actions,
  bottom = <LogoutButton isCompact={true} />,
  className,
  contentClassName,
  topClassName,
}: SlimRailSidebarProps) => (
  <aside
    className={cn(
      'hidden h-screen w-20 shrink-0 border-r border-white/20 bg-linear-to-b from-white/10 to-black/20 backdrop-blur-xl backdrop-saturate-150 md:block',
      className,
    )}>
    <div className={cn('flex h-full min-h-0 flex-col items-center p-2', contentClassName)}>
      <div className={cn('flex w-full flex-col items-center gap-3', topClassName)}>
        {header}
        {controls}
        {nav}
        {actions}
      </div>

      <div className="mt-auto flex w-full justify-center pb-1">{bottom}</div>
    </div>
  </aside>
)
