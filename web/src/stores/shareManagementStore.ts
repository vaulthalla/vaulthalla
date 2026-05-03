import { create } from 'zustand'
import { ShareLink, ShareLinkCreatePayload, ShareLinkUpdatePayload } from '@/models/linkShare'
import { useWebSocketStore } from '@/stores/useWebSocket'

interface ShareManagementStore {
  shares: ShareLink[]
  selectedShare: ShareLink | null
  loading: boolean
  error: string | null
  createShare: (payload: ShareLinkCreatePayload) => Promise<{ share: ShareLink; publicToken: string; publicUrlPath: string }>
  fetchShares: (payload?: { vault_id?: number | null; limit?: number; offset?: number }) => Promise<void>
  getShare: (id: string) => Promise<ShareLink>
  updateShare: (payload: ShareLinkUpdatePayload) => Promise<ShareLink>
  revokeShare: (id: string) => Promise<void>
  rotateToken: (id: string) => Promise<{ share: ShareLink; publicToken: string; publicUrlPath: string }>
}

export const useShareManagementStore = create<ShareManagementStore>()((set, get) => ({
  shares: [],
  selectedShare: null,
  loading: false,
  error: null,

  async createShare(payload) {
    const ws = useWebSocketStore.getState()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('share.link.create', payload)
      set({ shares: [response.share, ...get().shares], selectedShare: response.share, loading: false })
      return {
        share: response.share,
        publicToken: response.public_token,
        publicUrlPath: response.public_url_path,
      }
    } catch (error) {
      set({ loading: false, error: error instanceof Error ? error.message : 'Unable to create share' })
      throw error
    }
  },

  async fetchShares(payload = {}) {
    const ws = useWebSocketStore.getState()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('share.link.list', payload)
      set({ shares: response.shares, loading: false })
    } catch (error) {
      set({ loading: false, error: error instanceof Error ? error.message : 'Unable to fetch shares' })
      throw error
    }
  },

  async getShare(id) {
    const ws = useWebSocketStore.getState()
    const response = await ws.sendCommand('share.link.get', { id })
    set({ selectedShare: response.share })
    return response.share
  },

  async updateShare(payload) {
    const ws = useWebSocketStore.getState()
    set({ loading: true, error: null })
    try {
      const response = await ws.sendCommand('share.link.update', payload)
      set(state => ({
        shares: state.shares.map(share => (share.id === response.share.id ? response.share : share)),
        selectedShare: response.share,
        loading: false,
      }))
      return response.share
    } catch (error) {
      set({ loading: false, error: error instanceof Error ? error.message : 'Unable to update share' })
      throw error
    }
  },

  async revokeShare(id) {
    const ws = useWebSocketStore.getState()
    await ws.sendCommand('share.link.revoke', { id })
    set(state => ({
      shares: state.shares.map(share => (share.id === id ? { ...share, revoked_at: new Date().toISOString() } : share)),
    }))
  },

  async rotateToken(id) {
    const ws = useWebSocketStore.getState()
    const response = await ws.sendCommand('share.link.rotate_token', { id })
    set(state => ({
      shares: state.shares.map(share => (share.id === response.share.id ? response.share : share)),
      selectedShare: response.share,
    }))
    return {
      share: response.share,
      publicToken: response.public_token,
      publicUrlPath: response.public_url_path,
    }
  },
}))
