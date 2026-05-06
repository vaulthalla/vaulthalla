import { create } from 'zustand'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { VaultStats } from '@/models/stats/vaultStats'
import { VaultActivity } from '@/models/stats/vaultActivity'
import { VaultRecovery } from '@/models/stats/vaultRecovery'
import { VaultSecurity } from '@/models/stats/vaultSecurity'
import { VaultShareStats } from '@/models/stats/vaultShareStats'
import { VaultSyncHealth } from '@/models/stats/vaultSyncHealth'
import { CacheStats } from '@/models/stats/cacheStats'
import { ConnectionStats } from '@/models/stats/connectionStats'
import { DashboardOverview } from '@/models/stats/dashboardOverview'
import { DbStats } from '@/models/stats/dbStats'
import { FuseStats } from '@/models/stats/fuseStats'
import { OperationStats } from '@/models/stats/operationStats'
import { RetentionStats } from '@/models/stats/retentionStats'
import { StatsTrends } from '@/models/stats/statsTrends'
import { StorageBackendStats } from '@/models/stats/storageBackendStats'
import { SystemHealth } from '@/models/stats/systemHealth'
import { ThreadPoolManagerStats } from '@/models/stats/threadPoolStats'
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
export type SystemHealthWrapper = StatsWrapper<SystemHealth>
export type ThreadPoolStatsWrapper = StatsWrapper<ThreadPoolManagerStats>
export type FuseStatsWrapper = StatsWrapper<FuseStats>
export type DbStatsWrapper = StatsWrapper<DbStats>
export type OperationStatsWrapper = StatsWrapper<OperationStats>
export type ConnectionStatsWrapper = StatsWrapper<ConnectionStats>
export type StorageBackendStatsWrapper = StatsWrapper<StorageBackendStats>
export type RetentionStatsWrapper = StatsWrapper<RetentionStats>
export type StatsTrendsWrapper = StatsWrapper<StatsTrends>
export type DashboardOverviewWrapper = StatsWrapper<DashboardOverview>

const makeCacheWrapper = (initial?: Partial<CacheStatsWrapper>): CacheStatsWrapper => ({
  data: new CacheStats({}),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeSystemHealthWrapper = (initial?: Partial<SystemHealthWrapper>): SystemHealthWrapper => ({
  data: new SystemHealth(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeThreadPoolStatsWrapper = (initial?: Partial<ThreadPoolStatsWrapper>): ThreadPoolStatsWrapper => ({
  data: new ThreadPoolManagerStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeFuseStatsWrapper = (initial?: Partial<FuseStatsWrapper>): FuseStatsWrapper => ({
  data: new FuseStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeDbStatsWrapper = (initial?: Partial<DbStatsWrapper>): DbStatsWrapper => ({
  data: new DbStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeOperationStatsWrapper = (initial?: Partial<OperationStatsWrapper>): OperationStatsWrapper => ({
  data: new OperationStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeConnectionStatsWrapper = (initial?: Partial<ConnectionStatsWrapper>): ConnectionStatsWrapper => ({
  data: new ConnectionStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeStorageBackendStatsWrapper = (
  initial?: Partial<StorageBackendStatsWrapper>,
): StorageBackendStatsWrapper => ({
  data: new StorageBackendStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeRetentionStatsWrapper = (initial?: Partial<RetentionStatsWrapper>): RetentionStatsWrapper => ({
  data: new RetentionStats(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeStatsTrendsWrapper = (initial?: Partial<StatsTrendsWrapper>): StatsTrendsWrapper => ({
  data: new StatsTrends(),
  lastUpdated: null,
  loading: false,
  error: null,
  lastSuccessAt: null,
  lastErrorAt: null,
  ...initial,
})

const makeDashboardOverviewWrapper = (initial?: Partial<DashboardOverviewWrapper>): DashboardOverviewWrapper => ({
  data: new DashboardOverview(),
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
  systemHealth: SystemHealthWrapper
  threadPoolStats: ThreadPoolStatsWrapper
  fuseStats: FuseStatsWrapper
  dbStats: DbStatsWrapper
  operationStats: OperationStatsWrapper
  connectionStats: ConnectionStatsWrapper
  storageBackendStats: StorageBackendStatsWrapper
  retentionStats: RetentionStatsWrapper
  systemTrends: StatsTrendsWrapper
  dashboardOverview: DashboardOverviewWrapper

  // Fetchers (raw RPC)
  getVaultStats: (payload: WSCommandPayload<'stats.vault'>) => Promise<VaultStats>
  getVaultSyncHealth: (payload: WSCommandPayload<'stats.vault.sync'>) => Promise<VaultSyncHealth>
  getVaultActivity: (payload: WSCommandPayload<'stats.vault.activity'>) => Promise<VaultActivity>
  getVaultShareStats: (payload: WSCommandPayload<'stats.vault.shares'>) => Promise<VaultShareStats>
  getVaultRecovery: (payload: WSCommandPayload<'stats.vault.recovery'>) => Promise<VaultRecovery>
  getVaultOperationStats: (payload: WSCommandPayload<'stats.vault.operations'>) => Promise<OperationStats>
  getVaultSecurity: (payload: WSCommandPayload<'stats.vault.security'>) => Promise<VaultSecurity>
  getVaultStorageBackendStats: (payload: WSCommandPayload<'stats.vault.storage'>) => Promise<StorageBackendStats>
  getVaultRetentionStats: (payload: WSCommandPayload<'stats.vault.retention'>) => Promise<RetentionStats>
  getVaultTrends: (payload: WSCommandPayload<'stats.vault.trends'>) => Promise<StatsTrends>
  getSystemHealth: (payload?: WSCommandPayload<'stats.system.health'>) => Promise<SystemHealth>
  getThreadPoolStats: (payload?: WSCommandPayload<'stats.system.threadpools'>) => Promise<ThreadPoolManagerStats>
  getFuseStats: (payload?: WSCommandPayload<'stats.system.fuse'>) => Promise<FuseStats>
  getDbStats: (payload?: WSCommandPayload<'stats.system.db'>) => Promise<DbStats>
  getOperationStats: (payload?: WSCommandPayload<'stats.system.operations'>) => Promise<OperationStats>
  getConnectionStats: (payload?: WSCommandPayload<'stats.system.connections'>) => Promise<ConnectionStats>
  getStorageBackendStats: (payload?: WSCommandPayload<'stats.system.storage'>) => Promise<StorageBackendStats>
  getRetentionStats: (payload?: WSCommandPayload<'stats.system.retention'>) => Promise<RetentionStats>
  getSystemTrends: (payload?: WSCommandPayload<'stats.system.trends'>) => Promise<StatsTrends>
  getDashboardOverview: (payload?: WSCommandPayload<'stats.dashboard.overview'>) => Promise<DashboardOverview>
  getFsCacheStats: (payload?: WSCommandPayload<'stats.fs.cache'>) => Promise<CacheStats>
  getHttpCacheStats: (payload?: WSCommandPayload<'stats.http.cache'>) => Promise<CacheStats>

  // Refresh helpers
  refreshSystemHealth: () => Promise<SystemHealth>
  refreshThreadPoolStats: () => Promise<ThreadPoolManagerStats>
  refreshFuseStats: () => Promise<FuseStats>
  refreshDbStats: () => Promise<DbStats>
  refreshOperationStats: () => Promise<OperationStats>
  refreshConnectionStats: () => Promise<ConnectionStats>
  refreshStorageBackendStats: () => Promise<StorageBackendStats>
  refreshRetentionStats: () => Promise<RetentionStats>
  refreshSystemTrends: () => Promise<StatsTrends>
  refreshDashboardOverview: (payload?: WSCommandPayload<'stats.dashboard.overview'>) => Promise<DashboardOverview>
  refreshFsCacheStats: () => Promise<CacheStats>
  refreshHttpCacheStats: () => Promise<CacheStats>

  // Polling (separate pollers)
  startSystemHealthPolling: (intervalMs?: number) => void
  stopSystemHealthPolling: () => void
  startThreadPoolStatsPolling: (intervalMs?: number) => void
  stopThreadPoolStatsPolling: () => void
  startFuseStatsPolling: (intervalMs?: number) => void
  stopFuseStatsPolling: () => void
  startDbStatsPolling: (intervalMs?: number) => void
  stopDbStatsPolling: () => void
  startOperationStatsPolling: (intervalMs?: number) => void
  stopOperationStatsPolling: () => void
  startConnectionStatsPolling: (intervalMs?: number) => void
  stopConnectionStatsPolling: () => void
  startStorageBackendStatsPolling: (intervalMs?: number) => void
  stopStorageBackendStatsPolling: () => void
  startRetentionStatsPolling: (intervalMs?: number) => void
  stopRetentionStatsPolling: () => void
  startSystemTrendsPolling: (intervalMs?: number) => void
  stopSystemTrendsPolling: () => void
  startDashboardOverviewPolling: (intervalMs?: number, payload?: WSCommandPayload<'stats.dashboard.overview'>) => void
  stopDashboardOverviewPolling: () => void
  startFsCacheStatsPolling: (intervalMs?: number) => void
  stopFsCacheStatsPolling: () => void
  startHttpCacheStatsPolling: (intervalMs?: number) => void
  stopHttpCacheStatsPolling: () => void

  // internal
  _systemHealthPoller: number | null
  _threadPoolStatsPoller: number | null
  _fuseStatsPoller: number | null
  _dbStatsPoller: number | null
  _operationStatsPoller: number | null
  _connectionStatsPoller: number | null
  _storageBackendStatsPoller: number | null
  _retentionStatsPoller: number | null
  _systemTrendsPoller: number | null
  _dashboardOverviewPoller: number | null
  _dashboardOverviewPayload: WSCommandPayload<'stats.dashboard.overview'> | null
  _fsCachePoller: number | null
  _httpCachePoller: number | null
}

export const useStatsStore = create<StatsStore>((set, get) => ({
  fsCacheStats: makeCacheWrapper(),
  httpCacheStats: makeCacheWrapper(),
  systemHealth: makeSystemHealthWrapper(),
  threadPoolStats: makeThreadPoolStatsWrapper(),
  fuseStats: makeFuseStatsWrapper(),
  dbStats: makeDbStatsWrapper(),
  operationStats: makeOperationStatsWrapper(),
  connectionStats: makeConnectionStatsWrapper(),
  storageBackendStats: makeStorageBackendStatsWrapper(),
  retentionStats: makeRetentionStatsWrapper(),
  systemTrends: makeStatsTrendsWrapper(),
  dashboardOverview: makeDashboardOverviewWrapper(),

  _systemHealthPoller: null,
  _threadPoolStatsPoller: null,
  _fuseStatsPoller: null,
  _dbStatsPoller: null,
  _operationStatsPoller: null,
  _connectionStatsPoller: null,
  _storageBackendStatsPoller: null,
  _retentionStatsPoller: null,
  _systemTrendsPoller: null,
  _dashboardOverviewPoller: null,
  _dashboardOverviewPayload: { scope: 'system', mode: 'dashboard_home' },
  _fsCachePoller: null,
  _httpCachePoller: null,

  async getVaultStats(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault', vault_id)
    return response.stats
  },

  async getVaultSyncHealth(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.sync', vault_id)
    return VaultSyncHealth.from(response.stats)
  },

  async getVaultActivity(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.activity', vault_id)
    return VaultActivity.from(response.stats)
  },

  async getVaultShareStats(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.shares', vault_id)
    return VaultShareStats.from(response.stats)
  },

  async getVaultRecovery(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.recovery', vault_id)
    return VaultRecovery.from(response.stats)
  },

  async getVaultOperationStats(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.operations', vault_id)
    return OperationStats.from(response.stats)
  },

  async getVaultSecurity(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.security', vault_id)
    return VaultSecurity.from(response.stats)
  },

  async getVaultStorageBackendStats(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.storage', vault_id)
    return StorageBackendStats.from(response.stats)
  },

  async getVaultRetentionStats(vault_id) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.retention', vault_id)
    return RetentionStats.from(response.stats)
  },

  async getVaultTrends(payload) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.vault.trends', payload)
    return StatsTrends.from(response.stats)
  },

  async getSystemHealth() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.health', null)
    return SystemHealth.from(response.stats)
  },

  async getThreadPoolStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.threadpools', null)
    return ThreadPoolManagerStats.from(response.stats)
  },

  async getFuseStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.fuse', null)
    return FuseStats.from(response.stats)
  },

  async getDbStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.db', null)
    return DbStats.from(response.stats)
  },

  async getOperationStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.operations', null)
    return OperationStats.from(response.stats)
  },

  async getConnectionStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.connections', null)
    return ConnectionStats.from(response.stats)
  },

  async getStorageBackendStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.storage', null)
    return StorageBackendStats.from(response.stats)
  },

  async getRetentionStats() {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.retention', null)
    return RetentionStats.from(response.stats)
  },

  async getSystemTrends(payload = { window_hours: 168 }) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.system.trends', payload)
    return StatsTrends.from(response.stats)
  },

  async getDashboardOverview(payload = { scope: 'system' as const, mode: 'dashboard_home' }) {
    const ws = useWebSocketStore.getState()
    await ws.waitForConnection()
    const response = await ws.sendCommand('stats.dashboard.overview', payload)
    return DashboardOverview.from(response.stats)
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

  async refreshSystemHealth() {
    const current = get().systemHealth
    if (current.loading) return current.data

    set({ systemHealth: { ...current, loading: true, error: null } })

    try {
      const health = await get().getSystemHealth()
      const nextData = SystemHealth.from(health ?? {})
      const now = Date.now()

      set({
        systemHealth: {
          ...get().systemHealth,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch system health'
      const now = Date.now()

      set({ systemHealth: { ...get().systemHealth, loading: false, error: msg, lastErrorAt: now } })

      return get().systemHealth.data
    }
  },

  async refreshThreadPoolStats() {
    const current = get().threadPoolStats
    if (current.loading) return current.data

    set({ threadPoolStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getThreadPoolStats()
      const nextData = ThreadPoolManagerStats.from(stats ?? {})
      const now = Date.now()

      set({
        threadPoolStats: {
          ...get().threadPoolStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch thread pool stats'
      const now = Date.now()

      set({ threadPoolStats: { ...get().threadPoolStats, loading: false, error: msg, lastErrorAt: now } })

      return get().threadPoolStats.data
    }
  },

  async refreshFuseStats() {
    const current = get().fuseStats
    if (current.loading) return current.data

    set({ fuseStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getFuseStats()
      const nextData = FuseStats.from(stats ?? {})
      const now = Date.now()

      set({
        fuseStats: {
          ...get().fuseStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch FUSE stats'
      const now = Date.now()

      set({ fuseStats: { ...get().fuseStats, loading: false, error: msg, lastErrorAt: now } })

      return get().fuseStats.data
    }
  },

  async refreshDbStats() {
    const current = get().dbStats
    if (current.loading) return current.data

    set({ dbStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getDbStats()
      const nextData = DbStats.from(stats ?? {})
      const now = Date.now()

      set({
        dbStats: {
          ...get().dbStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch database health'
      const now = Date.now()

      set({ dbStats: { ...get().dbStats, loading: false, error: msg, lastErrorAt: now } })

      return get().dbStats.data
    }
  },

  async refreshOperationStats() {
    const current = get().operationStats
    if (current.loading) return current.data

    set({ operationStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getOperationStats()
      const nextData = OperationStats.from(stats ?? {})
      const now = Date.now()

      set({
        operationStats: {
          ...get().operationStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch operation queue stats'
      const now = Date.now()

      set({ operationStats: { ...get().operationStats, loading: false, error: msg, lastErrorAt: now } })

      return get().operationStats.data
    }
  },

  async refreshConnectionStats() {
    const current = get().connectionStats
    if (current.loading) return current.data

    set({ connectionStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getConnectionStats()
      const nextData = ConnectionStats.from(stats ?? {})
      const now = Date.now()

      set({
        connectionStats: {
          ...get().connectionStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch connection stats'
      const now = Date.now()

      set({ connectionStats: { ...get().connectionStats, loading: false, error: msg, lastErrorAt: now } })

      return get().connectionStats.data
    }
  },

  async refreshStorageBackendStats() {
    const current = get().storageBackendStats
    if (current.loading) return current.data

    set({ storageBackendStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getStorageBackendStats()
      const nextData = StorageBackendStats.from(stats ?? {})
      const now = Date.now()

      set({
        storageBackendStats: {
          ...get().storageBackendStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch storage backend stats'
      const now = Date.now()

      set({ storageBackendStats: { ...get().storageBackendStats, loading: false, error: msg, lastErrorAt: now } })

      return get().storageBackendStats.data
    }
  },

  async refreshRetentionStats() {
    const current = get().retentionStats
    if (current.loading) return current.data

    set({ retentionStats: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getRetentionStats()
      const nextData = RetentionStats.from(stats ?? {})
      const now = Date.now()

      set({
        retentionStats: {
          ...get().retentionStats,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch retention pressure stats'
      const now = Date.now()

      set({ retentionStats: { ...get().retentionStats, loading: false, error: msg, lastErrorAt: now } })

      return get().retentionStats.data
    }
  },

  async refreshSystemTrends() {
    const current = get().systemTrends
    if (current.loading) return current.data

    set({ systemTrends: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getSystemTrends({ window_hours: 168 })
      const nextData = StatsTrends.from(stats ?? {})
      const now = Date.now()

      set({
        systemTrends: {
          ...get().systemTrends,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch trend snapshots'
      const now = Date.now()

      set({ systemTrends: { ...get().systemTrends, loading: false, error: msg, lastErrorAt: now } })

      return get().systemTrends.data
    }
  },

  async refreshDashboardOverview(payload) {
    const request = payload ?? get()._dashboardOverviewPayload ?? { scope: 'system' as const, mode: 'dashboard_home' }
    if (payload) set({ _dashboardOverviewPayload: request })

    const current = get().dashboardOverview
    if (current.loading) return current.data

    set({ dashboardOverview: { ...current, loading: true, error: null } })

    try {
      const stats = await get().getDashboardOverview(request)
      const nextData = DashboardOverview.from(stats ?? {})
      const now = Date.now()

      set({
        dashboardOverview: {
          ...get().dashboardOverview,
          data: nextData,
          lastUpdated: now,
          lastSuccessAt: now,
          loading: false,
          error: null,
        },
      })

      return nextData
    } catch (error: unknown) {
      const msg = getErrorMessage(error) || 'Failed to fetch dashboard overview'
      const now = Date.now()

      set({ dashboardOverview: { ...get().dashboardOverview, loading: false, error: msg, lastErrorAt: now } })

      return get().dashboardOverview.data
    }
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

  startSystemHealthPolling(intervalMs = 7500) {
    if (get()._systemHealthPoller) return

    void get().refreshSystemHealth()

    const id = window.setInterval(() => {
      void get().refreshSystemHealth()
    }, intervalMs)

    set({ _systemHealthPoller: id })
  },

  stopSystemHealthPolling() {
    const id = get()._systemHealthPoller
    if (id) {
      window.clearInterval(id)
      set({ _systemHealthPoller: null })
    }
  },

  startThreadPoolStatsPolling(intervalMs = 7500) {
    if (get()._threadPoolStatsPoller) return

    void get().refreshThreadPoolStats()

    const id = window.setInterval(() => {
      void get().refreshThreadPoolStats()
    }, intervalMs)

    set({ _threadPoolStatsPoller: id })
  },

  stopThreadPoolStatsPolling() {
    const id = get()._threadPoolStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _threadPoolStatsPoller: null })
    }
  },

  startFuseStatsPolling(intervalMs = 7500) {
    if (get()._fuseStatsPoller) return

    void get().refreshFuseStats()

    const id = window.setInterval(() => {
      void get().refreshFuseStats()
    }, intervalMs)

    set({ _fuseStatsPoller: id })
  },

  stopFuseStatsPolling() {
    const id = get()._fuseStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _fuseStatsPoller: null })
    }
  },

  startDbStatsPolling(intervalMs = 7500) {
    if (get()._dbStatsPoller) return

    void get().refreshDbStats()

    const id = window.setInterval(() => {
      void get().refreshDbStats()
    }, intervalMs)

    set({ _dbStatsPoller: id })
  },

  stopDbStatsPolling() {
    const id = get()._dbStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _dbStatsPoller: null })
    }
  },

  startOperationStatsPolling(intervalMs = 7500) {
    if (get()._operationStatsPoller) return

    void get().refreshOperationStats()

    const id = window.setInterval(() => {
      void get().refreshOperationStats()
    }, intervalMs)

    set({ _operationStatsPoller: id })
  },

  stopOperationStatsPolling() {
    const id = get()._operationStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _operationStatsPoller: null })
    }
  },

  startConnectionStatsPolling(intervalMs = 7500) {
    if (get()._connectionStatsPoller) return

    void get().refreshConnectionStats()

    const id = window.setInterval(() => {
      void get().refreshConnectionStats()
    }, intervalMs)

    set({ _connectionStatsPoller: id })
  },

  stopConnectionStatsPolling() {
    const id = get()._connectionStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _connectionStatsPoller: null })
    }
  },

  startStorageBackendStatsPolling(intervalMs = 7500) {
    if (get()._storageBackendStatsPoller) return

    void get().refreshStorageBackendStats()

    const id = window.setInterval(() => {
      void get().refreshStorageBackendStats()
    }, intervalMs)

    set({ _storageBackendStatsPoller: id })
  },

  stopStorageBackendStatsPolling() {
    const id = get()._storageBackendStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _storageBackendStatsPoller: null })
    }
  },

  startRetentionStatsPolling(intervalMs = 7500) {
    if (get()._retentionStatsPoller) return

    void get().refreshRetentionStats()

    const id = window.setInterval(() => {
      void get().refreshRetentionStats()
    }, intervalMs)

    set({ _retentionStatsPoller: id })
  },

  stopRetentionStatsPolling() {
    const id = get()._retentionStatsPoller
    if (id) {
      window.clearInterval(id)
      set({ _retentionStatsPoller: null })
    }
  },

  startSystemTrendsPolling(intervalMs = 30000) {
    if (get()._systemTrendsPoller) return

    void get().refreshSystemTrends()

    const id = window.setInterval(() => {
      void get().refreshSystemTrends()
    }, intervalMs)

    set({ _systemTrendsPoller: id })
  },

  stopSystemTrendsPolling() {
    const id = get()._systemTrendsPoller
    if (id) {
      window.clearInterval(id)
      set({ _systemTrendsPoller: null })
    }
  },

  startDashboardOverviewPolling(intervalMs = 7500, payload) {
    if (payload) set({ _dashboardOverviewPayload: payload })

    if (get()._dashboardOverviewPoller) {
      void get().refreshDashboardOverview(payload)
      return
    }

    void get().refreshDashboardOverview(payload)

    const id = window.setInterval(() => {
      void get().refreshDashboardOverview()
    }, intervalMs)

    set({ _dashboardOverviewPoller: id })
  },

  stopDashboardOverviewPolling() {
    const id = get()._dashboardOverviewPoller
    if (id) {
      window.clearInterval(id)
      set({ _dashboardOverviewPoller: null })
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
