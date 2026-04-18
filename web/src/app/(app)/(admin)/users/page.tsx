import UsersClientPage from '@/app/(app)/(admin)/users/page.client'
import { makeAdminPage } from '@/components/admin/makeAdminPage'

export default makeAdminPage(UsersClientPage, {
  title: 'Users',
  description: 'Manage users and their permissions in your vault.',
  grid: true,
  add: { title: 'Add User', href: '/users/add' },
})
