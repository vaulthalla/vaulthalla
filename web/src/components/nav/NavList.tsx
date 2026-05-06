import clsx from 'clsx'
import type { NavItem } from './types'
import { ActiveLink } from './ActiveLink.client'
import { DashboardNavSeverityBadge } from '@/components/nav/DashboardNavSeverityBadge'

export const NavList = ({ items, compact = false }: { items: NavItem[]; compact?: boolean }) => {
  const renderItem = (item: NavItem, depth: number) => {
    const Icon = item.icon
    const hasSub = !!item.subItems?.length

    return (
      <div key={item.href} className={clsx(depth === 0 && 'border-b border-cyan-900/30 pb-1')}>
        <ActiveLink
          href={item.href}
          partial={!item.exact}
          className={clsx(
            'relative flex items-center justify-between rounded-md px-2 py-2 transition-colors',
            compact && 'justify-center',
            depth === 0 ? 'text-base font-medium' : 'pl-4 text-sm font-normal',
            'text-cyan-300 hover:bg-cyan-700/30 hover:text-white',
          )}
          activeClassName="bg-cyan-700/80 font-semibold text-white shadow-inner">
          <span className={clsx('inline-flex items-center gap-2', compact && 'gap-0')}>
            <Icon className={clsx('fill-current text-cyan-300', compact ? 'h-6 w-6' : 'h-5 w-5')} />
            {!compact && item.label}
          </span>

          {compact && item.dashboardSeverity ?
            <DashboardNavSeverityBadge source={item.dashboardSeverity} compact />
          : null}

          {!compact && (item.dashboardSeverity || hasSub) && (
            <span className="inline-flex items-center gap-2">
              {item.dashboardSeverity ?
                <DashboardNavSeverityBadge source={item.dashboardSeverity} />
              : null}
              {hasSub && <span className="text-cyan-600">▾</span>}
            </span>
          )}
        </ActiveLink>

        {!compact && hasSub && (
          <div className="mt-1 ml-2 flex flex-col border-l border-cyan-900/30 pl-4">
            {item.subItems!.map(sub => renderItem(sub, depth + 1))}
          </div>
        )}
      </div>
    )
  }

  return (
    <div className={clsx('flex flex-col space-y-2', compact && 'items-center')}>{items.map(i => renderItem(i, 0))}</div>
  )
}
