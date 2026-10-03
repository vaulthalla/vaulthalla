'use client'

import React from 'react'
import { PageHeader } from '@/components/ui/Panel'
import { LinkTabs, type LinkTab } from '@/components/ui/Tabs'
import { ErrorState } from '@/components/ui/State'
import { ChartLineIcon, FolderTreeIcon, GaugeHighIcon, HardDriveIcon, MicrochipIcon } from '@/components/ui/icons'
import { useCan } from '@/lib/permissions'
import { WsError } from '@/lib/ws/errors'

const TABS: LinkTab[] = [
  { href: '/health', label: 'Overview', icon: GaugeHighIcon, exact: true },
  { href: '/health/runtime', label: 'Runtime', icon: MicrochipIcon },
  { href: '/health/filesystem', label: 'Filesystem', icon: FolderTreeIcon },
  { href: '/health/storage', label: 'Storage', icon: HardDriveIcon },
  { href: '/health/activity', label: 'Activity', icon: ChartLineIcon },
]

// Every health command is admin-only on the server; non-admins get the denied state instead of a page of refusals.
export const HealthShell = ({ children }: { children: React.ReactNode }) => {
  const isAdmin = useCan({ admin: true })
  if (!isAdmin) return <ErrorState error={new WsError('denied', 'Health is available to administrators.')} />
  return (
    <div className="w-full">
      <PageHeader
        title="Health"
        description="Live runtime, filesystem, storage and activity telemetry. Severity comes from the daemon."
        className="mb-4"
      />
      <LinkTabs tabs={TABS} className="mb-6" />
      {children}
    </div>
  )
}
