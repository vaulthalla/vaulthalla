import { create } from 'zustand'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { persist } from 'zustand/middleware'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { AdminRole, VaultRole, Permission } from '@/models/role'

interface PermissionStore {
  userRoles: AdminRole[]
  vaultRoles: VaultRole[]
  permissions: Permission[]
  fetchVaultRoles: () => Promise<void>
  fetchUserRoles: () => Promise<void>
  fetchPermissions: () => Promise<void>
  addRole: (payload: WSCommandPayload<'role.add'>) => Promise<void>
  removeRole: (payload: WSCommandPayload<'role.delete'>) => Promise<void>
  updateRole: (payload: WSCommandPayload<'role.update'>) => Promise<void>
  getRole: (payload: WSCommandPayload<'role.get'>) => Promise<AdminRole | VaultRole | null | undefined>
  getRoleByName: (payload: WSCommandPayload<'role.get.byName'>) => Promise<AdminRole | VaultRole | null | undefined>
  getPermission: (payload: WSCommandPayload<'permission.get'>) => Promise<Permission | null | undefined>
  getPermissionByName: (payload: WSCommandPayload<'permission.get.byName'>) => Promise<Permission | null | undefined>
  getPermissions: () => Promise<Permission[]>
}

export const usePermsStore = create<PermissionStore>()(
  persist(
    (set, get) => ({
      vaultRoles: [],
      userRoles: [],
      permissions: [],

      async fetchVaultRoles() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('roles.vault.list', null)
        set({ vaultRoles: response.roles })
      },

      async fetchUserRoles() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('roles.admin.list', null)
        console.log(response.roles)
        set({ userRoles: response.roles })
      },

      async fetchPermissions() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('permissions.list', null)
        set({ permissions: response.permissions.map(Permission.fromData) })
      },

      async addRole(payload) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('role.add', payload)

        if (payload.type === 'vault') await this.fetchVaultRoles()
        else if (payload.type === 'admin') await this.fetchUserRoles()
      },

      async removeRole({ id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('role.delete', { id })

        set(state => ({
          vaultRoles: state.vaultRoles.filter(r => r.id !== id),
          userRoles: state.userRoles.filter(r => r.id !== id),
        }))
      },

      async updateRole(payload) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('role.update', payload)

        if (payload.type === 'vault') await this.fetchVaultRoles()
        else if (payload.type === 'admin') await this.fetchUserRoles()
      },

      async getRole({ id }) {
        const role = get().vaultRoles.find(r => r.id === id) || get().userRoles.find(r => r.id === id)
        if (role) return role

        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('role.get', { id })
        return response.role
      },

      async getRoleByName({ name }) {
        const role = get().vaultRoles.find(r => r.name === name) || get().userRoles.find(r => r.name === name)
        if (role) return role

        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('role.get.byName', { name })
        return response.role
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
      partialize: state => ({
        vaultRoles: state.vaultRoles,
        userRoles: state.userRoles,
        permissions: state.permissions,
      }),
      onRehydrateStorage: state => {
        console.log('[PermissionStore] Rehydrating...')
        return async () => {
          try {
            await useWebSocketStore.getState().waitForConnection()

            await state?.fetchVaultRoles()
            await state?.fetchUserRoles?.()
            await state?.fetchPermissions?.()

            console.log('[PermissionStore] Roles and permissions fetch complete')
          } catch (err) {
            console.error('[PermissionStore] Rehydrate fetch failed:', err)
          }
        }
      },
    },
  ),
)
