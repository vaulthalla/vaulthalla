import EditVaultRoleClientPage from '@/app/(app)/(admin)/roles/vault/[role_id]/page.client'

const EditVaultRolePage = async ({ params }: { params: Promise<{ role_id: string }> }) => {
  const { role_id } = await params
  const id = Number(role_id)

  return <EditVaultRoleClientPage id={id} />
}

export default EditVaultRolePage
