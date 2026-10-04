'use client'

import React from 'react'
import Link from 'next/link'
import { usePathname } from 'next/navigation'
import * as RT from '@radix-ui/react-tabs'
import { cn } from '@/util/cn'

const listClass = 'flex max-w-full gap-1 overflow-x-auto border-b border-line scrollbar-thin'
const tabClass =
  'relative -mb-px inline-flex h-10 shrink-0 items-center gap-2 border-b-2 border-transparent px-3 text-sm text-fg-subtle transition-colors hover:text-fg data-[state=active]:border-accent data-[state=active]:text-fg aria-[current=page]:border-accent aria-[current=page]:text-fg [&_svg]:size-4'

export const Tabs = RT.Root
export const TabsContent = ({ className, ...props }: React.ComponentPropsWithoutRef<typeof RT.Content>) => (
  <RT.Content className={cn('pt-5 focus:outline-none', className)} {...props} />
)
export const TabsList = ({ className, ...props }: React.ComponentPropsWithoutRef<typeof RT.List>) => (
  <RT.List className={cn(listClass, className)} {...props} />
)
export const TabsTrigger = ({ className, ...props }: React.ComponentPropsWithoutRef<typeof RT.Trigger>) => (
  <RT.Trigger className={cn(tabClass, className)} {...props} />
)

export interface LinkTab {
  href: string
  label: string
  icon?: React.ComponentType<React.SVGProps<SVGSVGElement>>
  exact?: boolean
}

// Route-backed tabs: each tab is a real URL (back/refresh/deep links work).
export const LinkTabs = ({ tabs, className }: { tabs: LinkTab[]; className?: string }) => {
  const pathname = usePathname() ?? ''
  return (
    <nav className={cn(listClass, className)} aria-label="Sections">
      {tabs.map(tab => {
        const active = tab.exact ? pathname === tab.href : pathname === tab.href || pathname.startsWith(`${tab.href}/`)
        return (
          <Link key={tab.href} href={tab.href} aria-current={active ? 'page' : undefined} className={tabClass}>
            {tab.icon ? <tab.icon aria-hidden /> : null}
            {tab.label}
          </Link>
        )
      })}
    </nav>
  )
}

// Compact segmented control for filters (e.g. Admin / Vault roles).
export const Segmented = <T extends string>({
  value,
  onChange,
  options,
  label,
}: {
  value: T
  onChange: (value: T) => void
  options: { value: T; label: string }[]
  label: string
}) => (
  <div role="radiogroup" aria-label={label} className="inline-flex rounded-control border border-line bg-surface-1 p-0.5">
    {options.map(option => (
      <button
        key={option.value}
        type="button"
        role="radio"
        aria-checked={value === option.value}
        onClick={() => onChange(option.value)}
        className={cn(
          'h-7 rounded-[6px] px-3 text-[13px] text-fg-subtle transition-colors hover:text-fg',
          value === option.value && 'bg-surface-3 text-fg shadow-[inset_0_1px_0_rgb(255_255_255/0.06)]',
        )}>
        {option.label}
      </button>
    ))}
  </div>
)
