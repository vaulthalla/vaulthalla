import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { AdminRole } from '@/models/role'
import { WSCommandPayload } from '@/util/webSocketCommands'

interface AdminRoleStore {
  adminRoles: AdminRole[]
  fetchAdminRoles: () => Promise<void>
  addAdminRole: (payload: WSCommandPayload<'role.admin.add'>) => Promise<void>
  removeAdminRole: (payload: WSCommandPayload<'role.admin.delete'>) => Promise<void>
  updateAdminRole: (payload: WSCommandPayload<'role.admin.update'>) => Promise<void>
  getAdminRole: (payload: WSCommandPayload<'role.admin.get'>) => Promise<AdminRole | null | undefined>
  getAdminRoleByName: (payload: WSCommandPayload<'role.admin.get.byName'>) => Promise<AdminRole | null | undefined>
}

export const useAdminRoleStore = create<AdminRoleStore>()(
  persist(
    (set, get) => ({
      adminRoles: [],

      async fetchAdminRoles() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('roles.admin.list', null)
        set({ adminRoles: response.roles.map(r => AdminRole.fromData(r)) })
      },

      async addAdminRole(payload) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        await ws.sendCommand('role.admin.add', payload)
        await get().fetchAdminRoles()
      },

      async removeAdminRole({ id }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        await ws.sendCommand('role.admin.delete', { id })

        set(state => ({ adminRoles: state.adminRoles.filter(r => r.id !== id) }))
      },

      async updateAdminRole(payload) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        await ws.sendCommand('role.admin.update', payload)
        await get().fetchAdminRoles()
      },

      async getAdminRole({ id }) {
        const cached = get().adminRoles.find(r => r.id === id)
        if (cached) return cached

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('role.admin.get', { id })
        return AdminRole.fromData(response.role)
      },

      async getAdminRoleByName({ name }) {
        const cached = get().adminRoles.find(r => r.name === name)
        if (cached) return cached

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('role.admin.get.byName', { name })
        return AdminRole.fromData(response.role)
      },
    }),
    {
      name: 'vaulthalla-admin-roles',
      partialize: state => ({ adminRoles: state.adminRoles }),
      onRehydrateStorage: state => {
        console.log('[AdminRoleStore] Rehydrating...')
        return async () => {
          try {
            await useWebSocketStore.getState().waitForConnection()
            await state?.fetchAdminRoles?.()
            console.log('[AdminRoleStore] Admin roles fetch complete')
          } catch (err) {
            console.error('[AdminRoleStore] Rehydrate fetch failed:', err)
          }
        }
      },
    },
  ),
)
