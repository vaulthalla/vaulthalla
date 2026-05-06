import type { ComponentType, SVGProps } from 'react'

export type NavIcon = ComponentType<SVGProps<SVGSVGElement>>

export type DashboardNavSeveritySource =
  | { kind: 'overall' }
  | { kind: 'section'; sectionId: string }

export type NavItem = {
  href: string
  label: string
  icon: NavIcon
  exact?: boolean
  dashboardSeverity?: DashboardNavSeveritySource
  subItems?: NavItem[]
}

export type NavConfig = { items: NavItem[] }
