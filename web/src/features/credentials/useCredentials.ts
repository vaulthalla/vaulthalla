'use client'

import { useMemo } from 'react'
import { useWs } from '@/lib/query'
import { api } from '@/lib/session'
import { useCan } from '@/lib/permissions'
import { WsError, isWsError } from '@/lib/ws/errors'
import type { Payload } from '@/lib/ws/client'
import { parseApiKeyList, toCredential } from '@/features/credentials/types'

export const KEY_PERMS = {
  view: ['admin.keys.api.self.view', 'admin.keys.api.user.view', 'admin.keys.api.admin.view'],
  create: ['admin.keys.api.self.create', 'admin.keys.api.user.create', 'admin.keys.api.admin.create'],
  edit: ['admin.keys.api.self.edit', 'admin.keys.api.user.edit', 'admin.keys.api.admin.edit'],
  remove: ['admin.keys.api.self.remove', 'admin.keys.api.user.remove', 'admin.keys.api.admin.remove'],
}

export const useCredentialPerms = () => ({
  view: useCan({ prefix: 'admin.keys.api' }),
  create: useCan({ anyOf: KEY_PERMS.create }),
  edit: useCan({ anyOf: KEY_PERMS.edit }),
  remove: useCan({ anyOf: KEY_PERMS.remove }),
})

export const useCredentialList = (enabled = true) =>
  useWs('storage.apiKey.list', null, { enabled, select: data => parseApiKeyList(data.keys) })

export const useCredential = (id: number) =>
  useWs('storage.apiKey.get', { id }, { enabled: id > 0, select: data => toCredential(data.api_key) })

export interface VaultRef {
  id: number
  name: string
}

// Which vaults are bound to each credential. Vault visibility is its own permission, so this can be unavailable
// (null) while the credentials themselves load fine.
export const useCredentialUsage = (enabled = true) => {
  const vaults = useWs('storage.vault.list', null, { enabled, retry: false })
  const usage = useMemo(() => {
    if (!vaults.data) return null
    const map = new Map<number, VaultRef[]>()
    for (const vault of vaults.data.vaults ?? []) {
      const keyId = (vault as unknown as { api_key_id?: unknown }).api_key_id
      if (typeof keyId !== 'number' || keyId <= 0) continue
      map.set(keyId, [...(map.get(keyId) ?? []), { id: vault.id, name: vault.name }])
    }
    return map
  }, [vaults.data])
  return { usage, pending: vaults.isPending && enabled }
}

const UNKNOWN_COMMAND = /unknown command/i

export class DaemonTooOldError extends WsError {
  constructor() {
    super(
      'error',
      'This daemon is too old to edit credentials in place. Upgrade Vaulthalla to edit it; deleting and re-creating the credential is unsafe because it drops every vault that uses it.',
    )
  }
}

// #137: an edit is storage.apiKey.update (same id, vault bindings survive). Never fall back to remove + add.
export const updateCredential = async (payload: Payload<'storage.apiKey.update'>) => {
  try {
    return await api.send('storage.apiKey.update', payload)
  } catch (error) {
    if (isWsError(error) && UNKNOWN_COMMAND.test(error.message)) throw new DaemonTooOldError()
    throw error
  }
}
