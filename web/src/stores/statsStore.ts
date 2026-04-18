import { create } from 'zustand'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { VaultStats } from '@/models/stats/vaultStats'
import { CacheStats } from '@/models/stats/cacheStats'
import { getErrorMessage } from '@/util/handleErrors'

/**
 * Generic wrapper that carries:
 * - the actual stats payload
 * - UI metadata (loading/error/lastUpdated)
 * - optional polling handle
 */
export interface StatsWrapper<T> {
  data: T
  lastUpdated: number | null
  loading: boolean
  error: string | null

  // Optional extras you may want later:
  lastSuccessAt?: number | null
  lastErrorAt?: number | null
}

export type CacheStatsWrapper = StatsWrapper<CacheStats>

const makeCacheWrapper = (initial?: Partial<CacheStatsWrapper>): CacheStatsWrapper => ({
  data: new CacheStats({}),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

interface StatsStore {
  // Two independent caches, same type
  fsCacheStats: CacheStatsWrapper
  httpCacheStats: CacheStatsWrapper

  // Fetchers (raw RPC)
  getVaultStats: (payload: WSCommandPayload<'stats.vault'>) => Promise<VaultStats>
  getFsCacheStats: (payload?: WSCommandPayload<'stats.fs.cache'>) => Promise<CacheStats>
  getHttpCacheStats: (payload?: WSCommandPayload<'stats.http.cache'>) => Promise<CacheStats>

  // Refresh helpers
  refreshFsCacheStats: () => Promise<CacheStats>
  refreshHttpCacheStats: () => Promise<CacheStats>

  // Polling (separate pollers)
  startFsCacheStatsPolling: (intervalMs?: number) => void
  stopFsCacheStatsPolling: () => void
  startHttpCacheStatsPolling: (intervalMs?: number) => void
  stopHttpCacheStatsPolling: () => void

  // internal
  _fsCachePoller: number | null
  _httpCachePoller: number | null
}

export const useStatsStore = create<StatsStore>((set, get) => ({
  fsCacheStats: makeCacheWrapper(),
  httpCacheStats: makeCacheWrapper(),

  _fsCachePoller: null,
  _httpCachePoller: null,

  async getVaultStats(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault', vault_id)
    return response.stats
  },

  async getFsCacheStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.fs.cache', null)
    return response.stats
  },

  async getHttpCacheStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.http.cache', null)
    return response.stats
  },

  async refreshFsCacheStats() {
    const current = get().fsCacheStats
    if (current.loading) return current.data // dogpile protection

    set({ fsCacheStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getFsCacheStats()
      const nextData = new CacheStats(stats ?? {})
      const now = Date.now()

      set({
        fsCacheStats: {
          ...get().fsCacheStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch FS cache stats'
      const now = Date.now()

      set({ fsCacheStats: { ...get().fsCacheStats, loading: false, error: msg, lastErrorAt: now } })

      return get().fsCacheStats.data
    }
  },

  async refreshHttpCacheStats() {
    const current = get().httpCacheStats
    if (current.loading) return current.data

    set({ httpCacheStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getHttpCacheStats()
      const nextData = new CacheStats(stats ?? {})
      const now = Date.now()

      set({
        httpCacheStats: {
          ...get().httpCacheStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch HTTP cache stats'
      const now = Date.now()

      set({ httpCacheStats: { ...get().httpCacheStats, loading: false, error: msg, lastErrorAt: now } })

      return get().httpCacheStats.data
    }
  },

  startFsCacheStatsPolling(intervalMs = 7500) {
    if (get()._fsCachePoller) return

    void get().refreshFsCacheStats()

    const id = window.setInterval(() => {
      void get().refreshFsCacheStats()
    }, intervalMs)

    set({ _fsCachePoller: id })
  },

  stopFsCacheStatsPolling() {
    const id = get()._fsCachePoller
    if (id) {
      window.clearInterval(id)
      set({ _fsCachePoller: null })
    }
  },

  startHttpCacheStatsPolling(intervalMs = 7500) {
    if (get()._httpCachePoller) return

    void get().refreshHttpCacheStats()

    const id = window.setInterval(() => {
      void get().refreshHttpCacheStats()
    }, intervalMs)

    set({ _httpCachePoller: id })
  },

  stopHttpCacheStatsPolling() {
    const id = get()._httpCachePoller
    if (id) {
      window.clearInterval(id)
      set({ _httpCachePoller: null })
    }
  },
}))
