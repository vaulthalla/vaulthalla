import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { WSCommandPayload } from '@/util/webSocketCommands'
import type { GroupRecord as Group } from '@/features/access/types'

interface PermissionStore {
  groups: Group[]
  fetchGroups: () => Promise<void>
  addGroup: (vaultPayload: WSCommandPayload<'group.add'>) => Promise<void>
  removeGroup: (payload: WSCommandPayload<'group.remove'>) => Promise<void>
  updateGroup: (payload: WSCommandPayload<'group.update'>) => Promise<void>
  getGroup: (payload: WSCommandPayload<'group.get'>) => Promise<Group | undefined>
  getGroupByName: (payload: WSCommandPayload<'group.get.byName'>) => Promise<Group | undefined>
  addGroupMember: (payload: WSCommandPayload<'group.member.add'>) => Promise<void>
  removeGroupMember: (payload: WSCommandPayload<'group.member.remove'>) => Promise<void>
  getGroupsByUser: (payload: WSCommandPayload<'groups.list.byUser'>) => Promise<Group[]>
}

export const useGroupStore = create<PermissionStore>()(
  persist(
    (set, get) => ({
      groups: [],

      async fetchGroups() {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('groups.list', null)
        set({ groups: response.groups })
      },

      async addGroup(groupPayload) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('group.add', groupPayload)
        set(state => ({ groups: [...state.groups, response.group] }))
      },

      async removeGroup({ id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('group.remove', { id })
        set(state => ({ groups: state.groups.filter(g => g.id !== id) }))
      },

      async updateGroup(groupPayload) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('group.update', groupPayload)
        const updatedGroup = response.group
        set(state => ({ groups: state.groups.map(g => (g.id === updatedGroup.id ? updatedGroup : g)) }))
      },

      async getGroup({ id }) {
        const group = get().groups.find(g => g.id === id)
        if (group) return group

        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('group.get', { id })
        return response.group
      },

      async getGroupByName({ name }) {
        const group = get().groups.find(g => g.name === name)
        if (group) return group

        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('group.get.byName', { name })
        return response.group
      },

      async addGroupMember({ group_id, user_id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('group.member.add', { group_id, user_id })
        const updatedGroup = response.group
        set(state => ({ groups: state.groups.map(g => (g.id === updatedGroup.id ? updatedGroup : g)) }))
      },

      async removeGroupMember({ group_id, user_id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('group.member.remove', { group_id, user_id })
        const updatedGroup = response.group
        set(state => ({ groups: state.groups.map(g => (g.id === updatedGroup.id ? updatedGroup : g)) }))
      },

      async getGroupsByUser({ user_id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('groups.list.byUser', { user_id })
        return response.groups
      },
    }),
    {
      name: 'vaulthalla-groups',
      partialize: state => ({ groups: state.groups }),
      onRehydrateStorage: () => state => {
        if (state) {
          ;(async () => {
            try {
              await (await import('@/stores/useWebSocket')).useWebSocketStore.getState().waitForConnection()
              await state.fetchGroups()
            } catch (err) {
              console.error('[PermissionStore] Rehydration failed:', err)
            }
          })()
        }
      },
    },
  ),
)
