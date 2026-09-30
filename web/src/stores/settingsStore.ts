import { create } from 'zustand'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { Settings } from '@/models/settings'
import { useWebSocketStore } from '@/stores/useWebSocket'

interface SettingsStore {
  getSettings: () => Promise<Settings | undefined>
  updateSettings: (payload: WSCommandPayload<'settings.update'>) => Promise<Settings>
}

export const useSettingsStore = create<SettingsStore>(() => ({
  async getSettings() {
    const sendCommand = useWebSocketStore.getState().sendCommand
    const response = await sendCommand('settings.get', null)
    return response.settings
  },

  async updateSettings(payload) {
    const sendCommand = useWebSocketStore.getState().sendCommand
    const response = await sendCommand('settings.update', payload)
    return response.settings
  },
}))
