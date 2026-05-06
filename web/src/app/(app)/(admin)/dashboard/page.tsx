import DashboardOverviewComponent from '@/components/dashboard/DashboardOverview'

const DashboardPage = () => {
  return (
    <div className="h-full min-h-screen w-full">
      <div className="mx-auto w-full max-w-[104rem]">
        <DashboardOverviewComponent />
      </div>
    </div>
  )
}

export default DashboardPage
