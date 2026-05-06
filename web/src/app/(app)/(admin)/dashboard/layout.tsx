import type { Metadata } from 'next'
import DashboardRouteToolbar from '@/components/dashboard/DashboardRouteToolbar'

export const metadata: Metadata = { title: 'Vaulthalla | Dashboard', description: 'The Final Cloud' }

export default function DashboardLayout({ children }: { children: React.ReactNode }) {
  return (
    <div className="-m-6 min-h-screen w-[calc(100%+3rem)] bg-black/10 lg:-m-8 lg:w-[calc(100%+4rem)]">
      <DashboardRouteToolbar />
      <div className="px-3 py-4 lg:px-5 lg:py-5">{children}</div>
    </div>
  )
}
