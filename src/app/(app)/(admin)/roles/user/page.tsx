import UserRolesClientPage from '@/app/(app)/(admin)/roles/user/page.client'
import { AdminPage } from '@/components/admin/AdminPage'
import { AddButton } from '@/components/admin/AddButton'

const RolesUserPage = () => {
  const title = 'User Roles'
  const description = 'Manage user roles and permissions.'
  const props = { title, description }

  return (
    <AdminPage {...props}>
      <AddButton title="Add User Role" href={`/roles/user/add`}></AddButton>
      <UserRolesClientPage />
    </AdminPage>
  )
}

export default RolesUserPage
