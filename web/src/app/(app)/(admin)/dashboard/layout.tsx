import type { Metadata } from 'next'

export const metadata: Metadata = { title: 'Vaulthalla | Dashboard', description: 'The Final Cloud' }

export default function DashboardLayout({ children }: { children: React.ReactNode }) {
  return <div className="min-h-screen w-full">{children}</div>
}
