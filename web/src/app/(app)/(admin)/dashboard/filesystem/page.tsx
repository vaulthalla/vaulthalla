import { DashboardDetailPage, DashboardDetailSection } from '@/components/dashboard/DashboardDetailPage'
import CacheStatsComponent from '@/components/stats/CacheStats'
import FuseStatsComponent from '@/components/stats/FuseStats'

const FilesystemDashboardPage = () => {
  return (
    <DashboardDetailPage
      title="Filesystem"
      subtitle="Full FUSE operation telemetry and cache cards for filesystem and HTTP preview surfaces."
      items={[
        { id: 'fuse', label: 'FUSE' },
        { id: 'fs-cache', label: 'FS Cache' },
        { id: 'http-cache', label: 'HTTP Cache' },
      ]}>
      <DashboardDetailSection id="fuse">
        <FuseStatsComponent />
      </DashboardDetailSection>
      <DashboardDetailSection id="fs-cache">
        <CacheStatsComponent source="fs" />
      </DashboardDetailSection>
      <DashboardDetailSection id="http-cache">
        <CacheStatsComponent source="http" />
      </DashboardDetailSection>
    </DashboardDetailPage>
  )
}

export default FilesystemDashboardPage
