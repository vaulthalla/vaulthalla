import type { DashboardPreferenceLayout } from '@/models/dashboard/dashboardLayout'

export const DASHBOARD_HOME_PREFERENCE_KEY = 'dashboard.home'

export interface DashboardPreferencePayload {
  preference_key?: string
}

export interface DashboardPreferenceUpdatePayload extends DashboardPreferencePayload {
  layout: DashboardPreferenceLayout
}

export interface IDashboardPreference {
  preference_key: string
  layout: DashboardPreferenceLayout | null
  created_at: number | string | null
  updated_at: number | string | null
  exists: boolean
}

function asObject(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {}
}

function asString(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback
}

function asBoolean(value: unknown, fallback = false): boolean {
  return typeof value === 'boolean' ? value : fallback
}

function asCheckedAt(value: unknown): number | string | null {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'string' && value.length > 0) return value
  return null
}

function asPreferenceLayout(value: unknown): DashboardPreferenceLayout | null {
  const obj = asObject(value)
  if (!Array.isArray(obj.cards)) return null
  return { cards: obj.cards as DashboardPreferenceLayout['cards'] }
}

export class DashboardPreference implements IDashboardPreference {
  preference_key = DASHBOARD_HOME_PREFERENCE_KEY
  layout: DashboardPreferenceLayout | null = null
  created_at: number | string | null = null
  updated_at: number | string | null = null
  exists = false

  constructor(data?: Partial<IDashboardPreference>) {
    if (!data) return
    this.preference_key = data.preference_key ?? this.preference_key
    this.layout = data.layout ?? this.layout
    this.created_at = data.created_at ?? this.created_at
    this.updated_at = data.updated_at ?? this.updated_at
    this.exists = data.exists ?? this.exists
  }

  static from(raw: unknown): DashboardPreference {
    const data = asObject(raw)
    return new DashboardPreference({
      preference_key: asString(data.preference_key, DASHBOARD_HOME_PREFERENCE_KEY),
      layout: asPreferenceLayout(data.layout),
      created_at: asCheckedAt(data.created_at),
      updated_at: asCheckedAt(data.updated_at),
      exists: asBoolean(data.exists, false),
    })
  }
}
