import React, { useEffect, useMemo, useState } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import { StorageBackendBody } from '@/components/stats/StorageBackendStats'
import { StorageBackendStats } from '@/models/stats/storageBackendStats'
import { useStatsStore } from '@/stores/statsStore'
import { getErrorMessage } from '@/util/handleErrors'

type StorageBackendProps = { vaultId: number; intervalMs?: number }

const LiveBadge = ({
  loading,
  error,
  lastUpdated,
}: {
  loading: boolean
  error: string | null
  lastUpdated: number | null
}) => (
  <div className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-3 py-1 text-xs text-white/70 backdrop-blur">
    <span
      className={[
        'h-1.5 w-1.5 rounded-full',
        error ? 'bg-rose-400/80'
        : loading ? 'bg-amber-300/80'
        : 'bg-emerald-400/80',
      ].join(' ')}
    />
    {error ? 'error' : loading ? 'updating...' : 'live'}
    {lastUpdated ? <span className="text-white/35">/</span> : null}
    {lastUpdated ? <span className="text-white/50">{new Date(lastUpdated).toLocaleTimeString()}</span> : null}
  </div>
)

export default function VaultStorageBackendComponent({ vaultId, intervalMs = 7500 }: StorageBackendProps) {
  const [stats, setStats] = useState<StorageBackendStats | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [lastUpdated, setLastUpdated] = useState<number | null>(null)

  useEffect(() => {
    let alive = true
    let inflight = false

    const refresh = async () => {
      if (inflight) return
      inflight = true
      setLoading(true)
      setError(null)

      try {
        const next = await useStatsStore.getState().getVaultStorageBackendStats({ vault_id: vaultId })
        if (!alive) return
        setStats(next)
        setLastUpdated(Date.now())
      } catch (err: unknown) {
        if (!alive) return
        setError(getErrorMessage(err) || 'Failed to fetch storage backend stats')
      } finally {
        inflight = false
        if (alive) setLoading(false)
      }
    }

    void refresh()
    const id = window.setInterval(() => void refresh(), intervalMs)

    return () => {
      alive = false
      window.clearInterval(id)
    }
  }, [vaultId, intervalMs])

  const data = useMemo(
    () =>
      stats ??
      new StorageBackendStats({
        overall_status: 'unknown',
        vaults: [{ vault_id: vaultId, backend_status: 'unknown' }],
      }),
    [stats, vaultId],
  )
  const right = <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />

  return (
    <StatsCard
      title="Storage Backend"
      subtitle="Vault backing provider status, free-space signal, and local/S3 configuration"
      right={right}
    >
      <div className="space-y-4">
        {error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {error}
          </div>
        ) : null}
        <StorageBackendBody stats={data} lastUpdated={lastUpdated} />
      </div>
    </StatsCard>
  )
}
