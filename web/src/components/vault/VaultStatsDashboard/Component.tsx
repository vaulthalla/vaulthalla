import { useStatsStore } from '@/stores/statsStore'
import CapacityStats from '@/components/vault/VaultStatsDashboard/CapacityStats/Component'
import { useEffect, useState } from 'react'
import { VaultStats } from '@/models/stats/vaultStats'
import SyncHealth from '@/components/vault/VaultStatsDashboard/SyncHealth/Component'
import VaultActivity from '@/components/vault/VaultStatsDashboard/VaultActivity/Component'
import OperationQueue from '@/components/vault/VaultStatsDashboard/OperationQueue/Component'
import RecoveryReadiness from '@/components/vault/VaultStatsDashboard/RecoveryReadiness/Component'
import RetentionPressure from '@/components/vault/VaultStatsDashboard/RetentionPressure/Component'
import ShareStats from '@/components/vault/VaultStatsDashboard/ShareStats/Component'
import StorageBackend from '@/components/vault/VaultStatsDashboard/StorageBackend/Component'
import Trends from '@/components/vault/VaultStatsDashboard/Trends/Component'
import VaultSecurity from '@/components/vault/VaultStatsDashboard/VaultSecurity/Component'

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
        <StorageBackend vaultId={vault_id} />
        <SyncHealth vaultId={vault_id} initialLatestEvent={stats.latest_sync_event} />
        <RecoveryReadiness vaultId={vault_id} />
        <VaultActivity vaultId={vault_id} />
        <OperationQueue vaultId={vault_id} />
        <ShareStats vaultId={vault_id} />
        <VaultSecurity vaultId={vault_id} />
        <RetentionPressure vaultId={vault_id} />
        <Trends vaultId={vault_id} />
      </div>
    )
  )
}

export default VaultStatsDashboard
