import { DashboardDetailPage, DashboardDetailSection } from '@/components/dashboard/DashboardDetailPage'
import DbHealthComponent from '@/components/stats/DbHealth'
import RetentionPressureComponent from '@/components/stats/RetentionPressure'
import StorageBackendStatsComponent from '@/components/stats/StorageBackendStats'

const StorageDashboardPage = () => {
  return (
    <DashboardDetailPage
      title="Storage"
      subtitle="Full storage backend, database health, and retention pressure telemetry."
      items={[
        { id: 'storage-backend', label: 'Storage Backend' },
        { id: 'database', label: 'Database' },
        { id: 'retention', label: 'Retention' },
      ]}>
      <DashboardDetailSection id="storage-backend">
        <StorageBackendStatsComponent />
      </DashboardDetailSection>
      <DashboardDetailSection id="database">
        <DbHealthComponent />
      </DashboardDetailSection>
      <DashboardDetailSection id="retention">
        <RetentionPressureComponent />
      </DashboardDetailSection>
    </DashboardDetailPage>
  )
}

export default StorageDashboardPage
