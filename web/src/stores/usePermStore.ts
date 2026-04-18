import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { Permission } from '@/models/role'
import { WSCommandPayload } from '@/util/webSocketCommands'

interface PermStore {
  permissions: Permission[]
  fetchPermissions: () => Promise<void>
  getPermission: (payload: WSCommandPayload<'permission.get'>) => Promise<Permission | null>
  getPermissionByName: (payload: WSCommandPayload<'permission.get.byName'>) => Promise<Permission | null>
  getPermissions: () => Promise<Permission[]>
}

export const usePermStore = create<PermStore>()(
  persist(
    (set, get) => ({
      permissions: [],

      async fetchPermissions() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('permissions.list', null)
        set({ permissions: response.permissions })
      },

      async getPermission({ id }) {
        return get().permissions.find(p => p.id === id) || null
      },

      async getPermissionByName({ name }) {
        return get().permissions.find(p => p.displayName === name) || null
      },

      async getPermissions() {
        return get().permissions
      },
    }),
    {
      name: 'vaulthalla-permissions',
      partialize: state => ({ permissions: state.permissions }),
      onRehydrateStorage: state => {
        console.log('[PermStore] Rehydrating...')
        return async () => {
          try {
            await useWebSocketStore.getState().waitForConnection()
            await state?.fetchPermissions?.()
            console.log('[PermStore] Permissions fetch complete')
          } catch (err) {
            console.error('[PermStore] Rehydrate fetch failed:', err)
          }
        }
      },
    },
  ),
)
