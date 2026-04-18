import UserRolesClientPage from '@/app/(app)/(admin)/roles/admin/page.client'
import { AdminPage } from '@/components/admin/AdminPage'
import { AddButton } from '@/components/admin/AddButton'

const AdminRolesServerPage = () => {
  const title = 'Admin Roles'
  const description = 'Manage administrative roles and permissions.'
  const props = { title, description }

  return (
    <AdminPage {...props}>
      <AddButton title="Add User Role" href={`/roles/admin/add`}></AddButton>
      <UserRolesClientPage />
    </AdminPage>
  )
}

export default AdminRolesServerPage
