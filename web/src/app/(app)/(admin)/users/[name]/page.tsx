import UserFullCard from '@/components/users/UserFullCard'
import { AdminPage } from '@/components/admin/AdminPage'

const UserPage = async ({ params }: { params: Promise<{ name: string }> }) => {
  const { name } = await params

  return (
    <AdminPage>
      <UserFullCard name={name} />
    </AdminPage>
  )
}

export default UserPage
