import GroupsClientPage from '@/app/(app)/(admin)/groups/page.client'
import { makeAdminPage } from '@/components/admin/makeAdminPage'

export default makeAdminPage(GroupsClientPage, {
  title: 'Manage Groups',
  description: 'Create and manage groups to organize users and permissions effectively.',
})
