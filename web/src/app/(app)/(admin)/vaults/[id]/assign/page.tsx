import AssignUserClientPage from '@/app/(app)/(admin)/vaults/[id]/assign/page.client'

const AssignUserPage = async ({ params }: { params: Promise<{ id: string }> }) => {
  const { id } = await params

  return (
    <div className="p-6">
      <h1 className="mb-4 text-4xl font-semibold">Assign Vault Role</h1>
      <AssignUserClientPage id={Number(id)} />
    </div>
  )
}

export default AssignUserPage
