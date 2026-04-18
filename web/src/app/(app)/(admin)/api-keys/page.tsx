import APIKeysClientPage from '@/app/(app)/(admin)/api-keys/page.client'
import { makeAdminPage } from '@/components/admin/makeAdminPage'

export default makeAdminPage(APIKeysClientPage, {
  title: 'API Keys',
  description: 'Manage your API keys for cloud mounts here. You can create, edit, and delete keys as needed.',
  add: { title: 'Add API Key', href: '/api-keys/add' },
})
