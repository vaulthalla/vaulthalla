import UserRolesClientPage from '@/app/(app)/(admin)/roles/user/page.client'
import { AdminPage } from '@/components/admin/AdminPage'

const RolesUserPage = () => {
  const title = 'User Roles'
  const description = 'Manage user roles and permissions.'
  const props = { title, description }

  return (
    <AdminPage {...props}>
      <UserRolesClientPage />
    </AdminPage>
  )
}

export default RolesUserPage
