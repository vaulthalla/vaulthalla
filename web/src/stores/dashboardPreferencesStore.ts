import { create } from 'zustand'

import {
  DASHBOARD_HOME_PREFERENCE_KEY,
  DashboardPreference,
  type DashboardPreferencePayload,
  type DashboardPreferenceUpdatePayload,
} from '@/models/dashboard/dashboardPreferences'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { getErrorMessage } from '@/util/handleErrors'

interface DashboardPreferencesState {
  preference: DashboardPreference | null
  loading: boolean
  saving: boolean
  error: string | null
  lastUpdated: number | null
  _loadPromise: Promise<DashboardPreference> | null

  getPreference: (payload?: DashboardPreferencePayload) => Promise<DashboardPreference>
  savePreference: (payload: DashboardPreferenceUpdatePayload) => Promise<DashboardPreference>
  resetPreference: (payload?: DashboardPreferencePayload) => Promise<boolean>
  clearError: () => void
}

export const useDashboardPreferencesStore = create<DashboardPreferencesState>((set, get) => ({
  preference: null,
  loading: false,
  saving: false,
  error: null,
  lastUpdated: null,
  _loadPromise: null,

  async getPreference(payload = { preference_key: DASHBOARD_HOME_PREFERENCE_KEY }) {
    const currentPromise = get()._loadPromise
    if (currentPromise) return currentPromise

    const promise = (async () => {
      set({ loading: true, error: null })
      try {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('dashboard.preferences.get', payload)
        const preference = DashboardPreference.from(response.preferences)
        set({ preference, loading: false, lastUpdated: Date.now(), _loadPromise: null })
        return preference
      } catch (error: unknown) {
        const msg = getErrorMessage(error) || 'Failed to load dashboard preferences'
        set({ loading: false, error: msg, _loadPromise: null })
        throw error
      }
    })()

    set({ _loadPromise: promise })
    return promise
  },

  async savePreference(payload) {
    set({ saving: true, error: null })
    try {
      const ws = useWebSocketStore.getState()
      await ws.waitForConnection()
      const response = await ws.sendCommand('dashboard.preferences.update', payload)
      const preference = DashboardPreference.from(response.preferences)
      set({ preference, saving: false, lastUpdated: Date.now() })
      return preference
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to save dashboard preferences'
      set({ saving: false, error: msg })
      throw error
    }
  },

  async resetPreference(payload = { preference_key: DASHBOARD_HOME_PREFERENCE_KEY }) {
    set({ saving: true, error: null })
    try {
      const ws = useWebSocketStore.getState()
      await ws.waitForConnection()
      const response = await ws.sendCommand('dashboard.preferences.reset', payload)
      set({ preference: null, saving: false, lastUpdated: Date.now() })
      return Boolean(response.reset)
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to reset dashboard preferences'
      set({ saving: false, error: msg })
      throw error
    }
  },

  clearError() {
    set({ error: null })
  },
}))
