'use client'

import Link from 'next/link'
import { usePathname } from 'next/navigation'
import type { ComponentType, SVGProps } from 'react'

import { DashboardNavSeverityBadge } from '@/components/nav/DashboardNavSeverityBadge'
import type { DashboardNavSeveritySource } from '@/components/nav/types'
import ChartLineIcon from '@/fa-duotone/chart-line.svg'
import DatabaseIcon from '@/fa-duotone/database.svg'
import FolderTreeIcon from '@/fa-duotone/folder-tree.svg'
import GaugeIcon from '@/fa-duotone/gauge-simple.svg'
import ServerIcon from '@/fa-duotone/server.svg'
import ShareNodesIcon from '@/fa-duotone/share-nodes.svg'

type DashboardRouteItem = {
  href: string
  label: string
  icon: ComponentType<SVGProps<SVGSVGElement>>
  exact?: boolean
  severity: DashboardNavSeveritySource
}

const dashboardRouteItems: DashboardRouteItem[] = [
  { href: '/dashboard', label: 'Overview', icon: GaugeIcon, exact: true, severity: { kind: 'overall' } },
  { href: '/dashboard/runtime', label: 'Runtime', icon: ServerIcon, severity: { kind: 'section', sectionId: 'runtime' } },
  { href: '/dashboard/filesystem', label: 'Filesystem', icon: FolderTreeIcon, severity: { kind: 'section', sectionId: 'filesystem' } },
  { href: '/dashboard/storage', label: 'Storage', icon: DatabaseIcon, severity: { kind: 'section', sectionId: 'storage' } },
  { href: '/dashboard/operations', label: 'Operations', icon: ShareNodesIcon, severity: { kind: 'section', sectionId: 'operations' } },
  { href: '/dashboard/trends', label: 'Trends', icon: ChartLineIcon, severity: { kind: 'section', sectionId: 'trends' } },
]

function isActive(pathname: string, item: DashboardRouteItem): boolean {
  if (item.exact) return pathname === item.href
  return pathname === item.href || pathname.startsWith(`${item.href}/`)
}

export default function DashboardRouteToolbar() {
  const pathname = usePathname()

  return (
    <nav
      aria-label="Dashboard sections"
      className="sticky top-0 z-20 border-b border-white/10 bg-zinc-950/78 px-3 py-2 shadow-[0_16px_45px_-35px_rgba(34,211,238,0.75)] backdrop-blur-xl lg:px-5">
      <div className="mx-auto w-full max-w-[104rem]">
        <div className="flex items-center justify-between gap-3 overflow-hidden rounded-2xl border border-white/10 bg-black/25 p-1.5">
          <div className="flex min-w-0 flex-1 items-center gap-1.5 overflow-x-auto">
            {dashboardRouteItems.map(item => {
              const active = isActive(pathname, item)
              const Icon = item.icon

              return (
                <Link
                  key={item.href}
                  href={item.href}
                  className={[
                    'group relative inline-flex min-h-8 min-w-32 shrink-0 items-center justify-center gap-2 rounded-xl border px-3 text-xs transition',
                    active ?
                      'border-cyan-200/35 bg-cyan-400/12 text-cyan-50 shadow-[0_0_22px_-14px_rgba(34,211,238,0.9)]'
                    : 'border-white/10 bg-white/[0.04] text-white/60 hover:border-cyan-200/25 hover:bg-cyan-400/8 hover:text-cyan-100',
                  ].join(' ')}>
                  <Icon className="h-3.5 w-3.5 shrink-0 fill-current" aria-hidden="true" />
                  <span>{item.label}</span>
                  <DashboardNavSeverityBadge source={item.severity} compact />
                </Link>
              )
            })}
          </div>
        </div>
      </div>
    </nav>
  )
}
