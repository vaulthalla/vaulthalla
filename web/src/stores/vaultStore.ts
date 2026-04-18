import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'

interface VaultStore {
  vaults: Vault[]
  fetchVaults: () => Promise<void>
  addVault: (vaultPayload: WSCommandPayload<'storage.vault.add'>) => Promise<void>
  updateVault: (payload: WSCommandPayload<'storage.vault.update'>) => Promise<void>
  removeVault: (payload: WSCommandPayload<'storage.vault.remove'>) => Promise<void>
  getVault: (payload: WSCommandPayload<'storage.vault.get'>) => Promise<LocalDiskVault | S3Vault | Vault>
  getLocalVault: () => Promise<LocalDiskVault | undefined>
  syncVault: (payload: WSCommandPayload<'storage.vault.sync'>) => Promise<void>
}

export const useVaultStore = create<VaultStore>()(
  persist(
    (set, get) => ({
      vaults: [],

      async fetchVaults() {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        const response = await ws.sendCommand('storage.vault.list', null)
        set({ vaults: response.vaults })
      },

      async addVault(vaultPayload) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('storage.vault.add', vaultPayload)
      },

      async updateVault(vault) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('storage.vault.update', vault)
        const updatedVault = response.vault
        set(state => ({ vaults: state.vaults.map(v => (v.id === updatedVault.id ? updatedVault : v)) }))
      },

      async removeVault({ id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('storage.vault.remove', { id })
      },

      async getVault({ id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        const response = await sendCommand('storage.vault.get', { id })
        return response.vault
      },

      async getLocalVault() {
        if (!get().vaults.length) await get().fetchVaults()
        const local = get().vaults.find(v => v.type === 'local' && v.is_active)

        if (local instanceof LocalDiskVault) return local
        if (local) return new LocalDiskVault(local)

        return undefined
      },

      async syncVault({ id }) {
        const sendCommand = useWebSocketStore.getState().sendCommand
        await sendCommand('storage.vault.sync', { id })
      },
    }),
    {
      name: 'vaulthalla-vaults', // 🔐 localStorage key
      partialize: state => ({ vaults: state.vaults }), // store only what matters
    },
  ),
)
