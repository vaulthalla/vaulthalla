'use client'

import React from 'react'
import { useVaultStore } from '@/stores/vaultStore'
import { useFSStore } from '@/stores/fsStore'

export const VaultSelector = () => {
  const { currVault, setCurrVault, setPath } = useFSStore()
  const { vaults } = useVaultStore()

  const onVaultChange = (e: React.ChangeEvent<HTMLSelectElement>) => {
    const selected = vaults.find(v => String(v.id) === e.target.value)
    if (selected) {
      setCurrVault(selected)
      setPath('') // Reset path when switching vault
    }
  }

  return (
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
  )
}
