'use client'

import { useFSStore } from '@/stores/fsStore'
import { useVaultStore } from '@/stores/vaultStore'
import { cn } from '@/util/cn'

const VaultBreadcrumbs = ({ className }: { className?: string }) => {
  const { currVault, setCurrVault, path, setPath } = useFSStore()
  const { vaults } = useVaultStore()

  const onVaultChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const selected = vaults.find(v => String(v.id) === e.target.value)
    if (selected) {
      setCurrVault(selected)
      setPath('') // Reset path when switching vault
    }
  }

  const parts = path.split('/').filter(Boolean)

  const VaultSelector = () =>
    vaults.length > 1 ?
      <div className="inline-flex items-center gap-2.5">
        <div className="flex items-center gap-1">
          <select value={currVault?.id || ''} onChange={onVaultChange} className="rounded border bg-transparent p-1">
            <option value="">Select Vault</option>
            {vaults.map(v => (
              <option key={v.id} value={v.id}>
                {v.name}
              </option>
            ))}
          </select>
        </div>
        <div>
          <span className="cursor-pointer text-cyan-400 hover:underline" onClick={() => setPath('')}>
            mnt
          </span>
        </div>
      </div>
    : vaults.length === 1 ?
      <span className="cursor-pointer font-medium text-cyan-400 hover:underline" onClick={() => setPath('')}>
        {currVault?.name}
      </span>
    : <span className="text-gray-500 italic">No Vaults</span>

  const Breadcrumbs = () =>
    parts.length > 0
    && parts.map((part, index) => {
      const fullPath = '/' + parts.slice(0, index + 1).join('/')
      return (
        <div key={index} className="flex items-center gap-1">
          {index === 0 && <span>/</span>}
          <span key={fullPath} className="flex items-center">
            <span className="cursor-pointer text-cyan-400 hover:underline" onClick={() => setPath(fullPath)}>
              {part}
            </span>
            {index < parts.length - 1 && <span className="mx-1">/</span>}
          </span>
        </div>
      )
    })

  return (
    <nav className={cn('flex items-center gap-2 text-sm text-gray-300', className)}>
      <VaultSelector />
      <Breadcrumbs />
    </nav>
  )
}

export default VaultBreadcrumbs
