import VaultsClientPage from '@/app/(app)/(admin)/vaults/page.client'
import { makeAdminPage } from '@/components/admin/makeAdminPage'

export default makeAdminPage(VaultsClientPage, {
  title: 'Vaults',
  description: 'Choose a vault to manage.',
  add: { title: 'Add Vault', href: '/vaults/add' },
  grid: true,
})
