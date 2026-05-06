import {
  DASHBOARD_LAYOUT_STORAGE_KEY,
  dashboardLayoutPreference,
  type DashboardLayoutCard,
  normalizeDashboardLayout,
} from '@/models/dashboard/dashboardLayout'
import {
  dashboardCardCatalog,
  defaultDashboardLayout,
} from '@/components/dashboard/dashboardCardCatalog'

export function makeDefaultLayout(): DashboardLayoutCard[] {
  const defaults = defaultDashboardLayout()
  return normalizeDashboardLayout(defaults, dashboardCardCatalog, defaults)
}

export function loadStoredLayout(): DashboardLayoutCard[] {
  const defaults = makeDefaultLayout()
  if (typeof window === 'undefined') return defaults

  try {
    const raw = window.localStorage.getItem(DASHBOARD_LAYOUT_STORAGE_KEY)
    if (!raw) return defaults
    return normalizeDashboardLayout(JSON.parse(raw), dashboardCardCatalog, defaults)
  } catch {
    return defaults
  }
}

export function storeLayout(layout: DashboardLayoutCard[]) {
  if (typeof window === 'undefined') return
  window.localStorage.setItem(DASHBOARD_LAYOUT_STORAGE_KEY, JSON.stringify(dashboardLayoutPreference(layout)))
}

export function layoutKey(layout: DashboardLayoutCard[]): string {
  return JSON.stringify(dashboardLayoutPreference(layout).cards)
}
