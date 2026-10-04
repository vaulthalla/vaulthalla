'use client'

import React from 'react'
import { api } from '@/lib/session'
import { invalidate } from '@/lib/query'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import type { ProviderCredential } from '@/features/credentials/types'
import type { VaultRef } from '@/features/credentials/useCredentials'

// Deleting a credential a vault still uses would (on daemons before the in-use guard) cascade and drop the vault's
// S3 binding, so the console refuses up front when it can see the usage, and names the risk when it can't.
export const deleteCredential = async (credential: ProviderCredential, usedBy: VaultRef[] | null): Promise<boolean> => {
  if (usedBy && usedBy.length > 0) {
    notify.info(
      `“${credential.name}” is still in use`,
      `Used by ${usedBy.map(vault => vault.name).join(', ')}. Move ${usedBy.length === 1 ? 'that vault' : 'those vaults'} to another credential or delete ${usedBy.length === 1 ? 'it' : 'them'} first.`,
    )
    return false
  }

  const ok = await confirm({
    title: `Delete “${credential.name}”?`,
    description: (
      <div className="space-y-2">
        <p>The stored access key and secret are destroyed. This can’t be undone.</p>
        {usedBy === null ?
          <p>
            You can’t see which vaults use this credential. If any vault does, the server refuses the delete (older
            daemons drop the vault’s S3 binding instead), so check with a vault administrator first.
          </p>
        : <p>No vault uses it.</p>}
      </div>
    ),
    confirmLabel: 'Delete credential',
    typeToConfirm: usedBy === null ? credential.name : undefined,
  })
  if (!ok) return false
  try {
    await api.send('storage.apiKey.remove', { id: credential.api_key_id })
    await invalidate('storage.apiKey.list', 'storage.apiKey.get', 'storage.vault.list')
    notify.success('Credential deleted', credential.name)
    return true
  } catch (error) {
    notify.error(error, 'Could not delete the credential')
    return false
  }
}
