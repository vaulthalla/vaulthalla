import UserForm from '@/components/users/UserForm'

const EditUserPage = async ({ params }: { params: Promise<{ name: string }> }) => {
  const { name } = await params

  return <UserForm name={name} />
}

export default EditUserPage
