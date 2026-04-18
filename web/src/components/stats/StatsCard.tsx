'use client'

import React from 'react'

import * as echarts from 'echarts/core'
import { BarChart, PieChart, ScatterChart } from 'echarts/charts'
import { GridComponent, TooltipComponent, LegendComponent, TitleComponent } from 'echarts/components'
import { CanvasRenderer } from 'echarts/renderers'

echarts.use([
  BarChart,
  PieChart,
  ScatterChart,
  GridComponent,
  TooltipComponent,
  LegendComponent,
  TitleComponent,
  CanvasRenderer,
])

type StatsCardProps = {
  title: string
  subtitle?: string
  right?: React.ReactNode
  children: React.ReactNode
  className?: string
}

export function StatsCard({ title, subtitle, right, children, className }: StatsCardProps) {
  return (
    <section
      className={[
        // smoky semi-opaque dark glass
        'relative overflow-hidden rounded-3xl border border-white/10',
        'bg-zinc-950/55 shadow-[0_20px_60px_-25px_rgba(0,0,0,0.9)] backdrop-blur-xl',
        // subtle “magical” rim light
        'before:pointer-events-none before:absolute before:inset-0',
        'before:bg-[radial-gradient(900px_circle_at_15%_10%,rgba(255,255,255,0.10),transparent_45%),radial-gradient(700px_circle_at_85%_25%,rgba(56,189,248,0.10),transparent_55%),radial-gradient(800px_circle_at_45%_120%,rgba(168,85,247,0.10),transparent_55%)]',
        // faint noise to avoid flat gradients (pure CSS)
        'after:pointer-events-none after:absolute after:inset-0 after:opacity-[0.10]',
        'after:bg-[url("data:image/svg+xml,%3Csvg xmlns=%27http://www.w3.org/2000/svg%27 width=%2760%27 height=%2760%27 viewBox=%270 0 60 60%27%3E%3Cfilter id=%27n%27 x=%270%27 y=%270%27%3E%3CfeTurbulence type=%27fractalNoise%27 baseFrequency=%270.9%27 numOctaves=%271%27 stitchTiles=%27stitch%27/%3E%3C/filter%3E%3Crect width=%2760%27 height=%2760%27 filter=%27url(%23n)%27 opacity=%270.35%27/%3E%3C/svg%3E")]',
        className ?? '',
      ].join(' ')}>
      <header className="relative z-10 flex items-start justify-between gap-4 p-5">
        <div className="min-w-0">
          <div className="text-sm font-semibold text-white/95">{title}</div>
          {subtitle ?
            <div className="mt-1 text-xs text-white/55">{subtitle}</div>
          : null}
        </div>
        {right ?
          <div className="shrink-0">{right}</div>
        : null}
      </header>

      <div className="relative z-10 px-5 pb-5">{children}</div>
    </section>
  )
}
