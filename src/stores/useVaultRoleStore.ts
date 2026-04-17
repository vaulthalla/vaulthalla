import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { VaultRole } from '@/models/role'
import { WSCommandPayload, WSCommandResponse } from '@/util/webSocketCommands'
import { VaultRoleDTO } from '@/models/permission'

interface VaultRoleStore {
  vaultRoles: VaultRole[]
  assignedRolesByVault: Record<number, VaultRole[]>
  fetchVaultRoles: () => Promise<void>
  fetchAssignedRoles: (payload: WSCommandPayload<'roles.vault.list.assigned'>) => Promise<VaultRole[]>
  addVaultRole: (payload: WSCommandPayload<'role.vault.add'>) => Promise<void>
  removeVaultRole: (payload: WSCommandPayload<'role.vault.delete'>) => Promise<void>
  updateVaultRole: (payload: WSCommandPayload<'role.vault.update'>) => Promise<void>
  getVaultRole: (payload: WSCommandPayload<'role.vault.get'>) => Promise<VaultRole | null | undefined>
  getVaultRoleByName: (payload: WSCommandPayload<'role.vault.get.byName'>) => Promise<VaultRole | null | undefined>
  getAssignedRoles: (payload: WSCommandPayload<'roles.vault.list.assigned'>) => Promise<VaultRole[]>
}

const normalizeAssignedRoles = (response: WSCommandResponse<'roles.vault.list.assigned'>): VaultRole[] => {
  const responseLike = response as WSCommandResponse<'roles.vault.list.assigned'> & {
    roles?: VaultRoleDTO[]
    vaults?: VaultRoleDTO[]
  }

  if (Array.isArray(responseLike.roles)) return responseLike.roles.map(role => VaultRole.fromData(role))
  if (Array.isArray(responseLike.vaults)) return responseLike.vaults.map(role => VaultRole.fromData(role))
  if (responseLike.vault) return [VaultRole.fromData(responseLike.vault)]
  return []
}

export const useVaultRoleStore = create<VaultRoleStore>()(
  persist(
    (set, get) => ({
      vaultRoles: [],
      assignedRolesByVault: {},

      async fetchVaultRoles() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('roles.vault.list', null)
        set({ vaultRoles: response.roles.map(r => VaultRole.fromData(r)) })
      },

      async fetchAssignedRoles({ id }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('roles.vault.list.assigned', { id })
        const roles = normalizeAssignedRoles(response)

        set(state => ({ assignedRolesByVault: { ...state.assignedRolesByVault, [id]: roles } }))

        return roles
      },

      async addVaultRole(payload) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        await ws.sendCommand('role.vault.add', payload)
        await get().fetchVaultRoles()
      },

      async removeVaultRole({ id }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        await ws.sendCommand('role.vault.delete', { id })

        set(state => ({ vaultRoles: state.vaultRoles.filter(r => r.id !== id) }))
      },

      async updateVaultRole(payload) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        await ws.sendCommand('role.vault.update', payload)
        await get().fetchVaultRoles()
      },

      async getVaultRole({ id }) {
        const cached = get().vaultRoles.find(r => r.id === id)
        if (cached) return cached

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('role.vault.get', { id })
        return VaultRole.fromData(response.vault)
      },

      async getVaultRoleByName({ name }) {
        const cached = get().vaultRoles.find(r => r.name === name)
        if (cached) return cached

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('role.vault.get.byName', { name })
        return VaultRole.fromData(response.vault)
      },

      async getAssignedRoles({ id }) {
        const cached = get().assignedRolesByVault[id]
        if (cached) return cached
        return get().fetchAssignedRoles({ id })
      },
    }),
    {
      name: 'vaulthalla-vault-roles',
      partialize: state => ({ vaultRoles: state.vaultRoles, assignedRolesByVault: state.assignedRolesByVault }),
      onRehydrateStorage: state => {
        console.log('[VaultRoleStore] Rehydrating...')
        return async () => {
          try {
            await useWebSocketStore.getState().waitForConnection()
            await state?.fetchVaultRoles?.()
            console.log('[VaultRoleStore] Vault roles fetch complete')
          } catch (err) {
            console.error('[VaultRoleStore] Rehydrate fetch failed:', err)
          }
        }
      },
    },
  ),
)
