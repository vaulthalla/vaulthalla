import { DashboardDetailPage, DashboardDetailSection } from '@/components/dashboard/DashboardDetailPage'
import OperationQueueStatsComponent from '@/components/stats/OperationQueueStats'

const OperationsDashboardPage = () => {
  return (
    <DashboardDetailPage
      title="Operations"
      subtitle="Full operation queue health for pending, active, failed, and stalled filesystem/share work."
      items={[{ id: 'operation-queue', label: 'Operation Queue' }]}>
      <DashboardDetailSection id="operation-queue">
        <OperationQueueStatsComponent />
      </DashboardDetailSection>
    </DashboardDetailPage>
  )
}

export default OperationsDashboardPage
