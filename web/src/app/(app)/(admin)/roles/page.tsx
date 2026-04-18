import { AdminPage } from '@/components/admin/AdminPage'
import { AdminGrid } from '@/components/admin/AdminGrid'
import { RoleCategoryCard } from '@/components/roles/RoleCategoryCard'
import UserIcon from '@/fa-duotone/user.svg'
import VaultIcon from '@/fa-duotone/vault.svg'

const RolesPage = () => {
  const title = 'Roles'
  const description = 'Choose a category to manage.'
  const props = { title, description }

  return (
    <AdminPage {...props}>
      <AdminGrid>
        <RoleCategoryCard
          title="Admin Roles"
          description="Administrative permissions and roles assigned to users."
          href="/roles/admin"
          Icon={UserIcon}
        />

        <RoleCategoryCard
          title="Vault Roles"
          description="Roles that apply within a vault context."
          href="/roles/vault"
          Icon={VaultIcon}
        />
      </AdminGrid>
    </AdminPage>
  )
}

export default RolesPage
