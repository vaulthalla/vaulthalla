import { useStatsStore } from '@/stores/statsStore'
import CapacityStats from '@/components/vault/VaultStatsDashboard/CapacityStats/Component'
import { useEffect, useState } from 'react'
import { VaultStats } from '@/models/stats/vaultStats'
import LatestSyncHealth from '@/components/vault/VaultStatsDashboard/LatestSyncHealth/Component'

const VaultStatsDashboard = ({ vault_id }: { vault_id: number }) => {
  const [stats, setStats] = useState<VaultStats | undefined>(undefined)

  useEffect(() => {
    const fetchStats = async () => setStats(await useStatsStore.getState().getVaultStats({ vault_id }))
    fetchStats()
  }, [vault_id])

  return (
    stats && (
      <div className="VaultStatsDashboard">
        <CapacityStats capacityStats={stats.capacity} />
        <LatestSyncHealth event={stats.latest_sync_event} />
      </div>
    )
  )
}

export default VaultStatsDashboard
