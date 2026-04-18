import EditAdminRoleClientPage from '@/app/(app)/(admin)/roles/admin/[role_id]/page.client'

const EditAdminRolePage = async ({ params }: { params: Promise<{ role_id: string }> }) => {
  const { role_id } = await params
  const id = Number(role_id)

  return <EditAdminRoleClientPage id={id} />
}

export default EditAdminRolePage
