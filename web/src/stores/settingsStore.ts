import { create } from 'zustand'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { Settings } from '@/models/settings'
import { useWebSocketStore } from '@/stores/useWebSocket'

interface PermissionStore {
  getSettings: () => Promise<Settings | undefined>
  updateSettings: (payload: WSCommandPayload<'settings.update'>) => Promise<void>
}

export const useSettingsStore = create<PermissionStore>(() => ({
  async getSettings() {
    const sendCommand = useWebSocketStore.getState().sendCommand
    const response = await sendCommand('settings.get', null)
    return Settings.fromData(response.settings)
  },

  async updateSettings(payload) {
    const sendCommand = useWebSocketStore.getState().sendCommand
    await sendCommand('settings.update', payload)
  },
}))
