import { DashboardDetailPage, DashboardDetailSection } from '@/components/dashboard/DashboardDetailPage'
import ConnectionStatsComponent from '@/components/stats/ConnectionStats'
import SystemHealthComponent from '@/components/stats/SystemHealth'
import ThreadPoolStatsComponent from '@/components/stats/ThreadPoolStats'

const RuntimeDashboardPage = () => {
  return (
    <DashboardDetailPage
      title="Runtime Services"
      subtitle="Full runtime health cards for service readiness, worker pressure, and websocket sessions."
      items={[
        { id: 'system-health', label: 'System Health' },
        { id: 'thread-pools', label: 'Thread Pools' },
        { id: 'connections', label: 'Connections' },
      ]}>
      <DashboardDetailSection id="system-health">
        <SystemHealthComponent />
      </DashboardDetailSection>
      <DashboardDetailSection id="thread-pools">
        <ThreadPoolStatsComponent />
      </DashboardDetailSection>
      <DashboardDetailSection id="connections">
        <ConnectionStatsComponent />
      </DashboardDetailSection>
    </DashboardDetailPage>
  )
}

export default RuntimeDashboardPage
