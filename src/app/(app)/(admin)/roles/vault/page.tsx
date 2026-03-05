import VaultRolesClientPage from '@/app/(app)/(admin)/roles/vault/page.client'
import { AdminPage } from '@/components/admin/AdminPage'

const VaultRolesPage = () => {
  const title = 'Vault Roles'
  const description = 'Manage vault roles and permissions'
  const props = { title, description }

  return (
    <AdminPage {...props}>
      <VaultRolesClientPage />
    </AdminPage>
  )
}

export default VaultRolesPage
