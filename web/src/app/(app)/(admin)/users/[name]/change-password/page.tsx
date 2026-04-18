import PasswordForm from '@/components/users/PasswordForm'

const ChangePasswordPage = async ({ params }: { params: Promise<{ name: string }> }) => {
  const { name } = await params

  return <PasswordForm name={name} />
}

export default ChangePasswordPage
