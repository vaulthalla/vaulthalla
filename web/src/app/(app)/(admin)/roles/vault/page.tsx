import VaultRolesClientPage from '@/app/(app)/(admin)/roles/vault/page.client'
import { AdminPage } from '@/components/admin/AdminPage'
import { AddButton } from '@/components/admin/AddButton'

const VaultRolesPage = () => {
  const title = 'Vault Roles'
  const description = 'Manage vault roles and permissions'
  const props = { title, description }

  return (
    <AdminPage {...props}>
      <AddButton title="Add Vault Role" href={`/roles/vault/add`}></AddButton>
      <VaultRolesClientPage />
    </AdminPage>
  )
}

export default VaultRolesPage
