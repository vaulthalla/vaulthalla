import type { Metadata } from 'next'

export const metadata: Metadata = { title: 'Vaulthalla | Dashboard', description: 'The Final Cloud' }

export default function DashboardLayout({ children }: { children: React.ReactNode }) {
  return <div className="flex h-screen flex-col items-center justify-center overflow-y-auto">{children}</div>
}
