import { DashboardDetailPage, DashboardDetailSection } from '@/components/dashboard/DashboardDetailPage'
import StatsTrendsComponent from '@/components/stats/StatsTrends'

const TrendsDashboardPage = () => {
  return (
    <DashboardDetailPage
      title="Trends"
      subtitle="Historical snapshots collected from live stats surfaces."
      items={[{ id: 'trends', label: 'Trends' }]}>
      <DashboardDetailSection id="trends">
        <StatsTrendsComponent />
      </DashboardDetailSection>
    </DashboardDetailPage>
  )
}

export default TrendsDashboardPage
