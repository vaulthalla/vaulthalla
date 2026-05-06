import Link from 'next/link'
import React from 'react'

type DashboardDetailItem = {
  id: string
  label: string
}

type DashboardDetailPageProps = {
  title: string
  subtitle: string
  items: DashboardDetailItem[]
  children: React.ReactNode
}

export function DashboardDetailPage({ title, subtitle, items, children }: DashboardDetailPageProps) {
  return (
    <div className="h-full min-h-screen w-full">
      <div className="mx-auto flex w-full max-w-[92rem] flex-col gap-5">
        <header className="flex flex-col gap-4 rounded-3xl border border-white/10 bg-zinc-950/55 p-5 shadow-[0_20px_60px_-25px_rgba(0,0,0,0.9)] backdrop-blur-xl">
          <div>
            <div className="text-xs font-semibold tracking-[0.18em] text-cyan-200/65 uppercase">Dashboard Drilldown</div>
            <h1 className="mt-2 text-2xl font-semibold text-white">{title}</h1>
            <p className="mt-2 max-w-3xl text-sm text-white/60">{subtitle}</p>
          </div>
          <nav className="flex flex-wrap gap-2" aria-label={`${title} sections`}>
            <Link
              href="/dashboard"
              className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-xs text-white/65 transition hover:border-cyan-300/30 hover:text-cyan-100">
              Overview
            </Link>
            {items.map(item => (
              <a
                key={item.id}
                href={`#${item.id}`}
                className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-xs text-white/65 transition hover:border-cyan-300/30 hover:text-cyan-100">
                {item.label}
              </a>
            ))}
          </nav>
        </header>

        {children}
      </div>
    </div>
  )
}

export function DashboardDetailSection({ id, children }: { id: string; children: React.ReactNode }) {
  return (
    <section id={id} className="scroll-mt-6">
      {children}
    </section>
  )
}
