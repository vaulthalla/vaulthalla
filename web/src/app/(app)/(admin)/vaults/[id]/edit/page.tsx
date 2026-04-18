import VaultEditPage from '@/app/(app)/(admin)/vaults/[id]/edit/page.client'

export default async function Page({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params
  return (
    <div className="p-6">
      <h1 className="mb-4 text-4xl font-semibold">Manage Vault</h1>
      <VaultEditPage id={Number(id)} />
    </div>
  )
}
