'use client'

import { useMemo } from 'react'
import { useParams } from 'next/navigation'
import { useWs } from '@/lib/query'
import { useCan, useIsAdmin } from '@/lib/permissions'
import { useSession } from '@/lib/session'
import { parseCredentials, type VaultDetail, type VaultRow } from '@/features/vaults/model'

// Every vault read goes through these so tabs share one cached copy of each command.

export const useVaultId = () => {
  const params = useParams<{ id: string }>()
  const id = Number(params?.id)
  return Number.isInteger(id) && id > 0 ? id : null
}

export const useVaultList = () =>
  useWs('storage.vault.list', null, { select: data => (data.vaults ?? []) as unknown as VaultRow[] })

export const useVault = (id: number) =>
  useWs('storage.vault.get', { id }, { select: data => data.vault as unknown as VaultDetail })

// Provider credentials (S3 API keys) visible to this account. Accounts without a key permission get none, quietly.
export const useCredentials = (enabled = true) => {
  const canView = useCan({ prefix: 'admin.keys.api' })
  return useWs('storage.apiKey.list', null, {
    enabled: enabled && canView,
    select: data => parseCredentials((data as { keys?: unknown }).keys),
    retry: false,
  })
}

// Names for user ids. auth.users.list needs admin.identities.users.view; without it we still know our own name.
export const useUserNames = () => {
  const canList = useCan({ permission: 'admin.identities.users.view' })
  const me = useSession(state => state.user)
  const users = useWs('auth.users.list', null, { enabled: canList, retry: false })
  return useMemo(() => {
    const names = new Map<number, string>()
    for (const user of users.data?.users ?? []) names.set(user.id, user.name)
    if (me && !names.has(me.id)) names.set(me.id, me.name)
    return { names, users: users.data?.users ?? [], canList, isPending: canList && users.isPending }
  }, [users.data, users.isPending, me, canList])
}

export const useGroupNames = () => {
  const canList = useCan({ permission: 'admin.identities.groups.view' })
  const groups = useWs('groups.list', null, { enabled: canList, retry: false })
  return useMemo(() => {
    const names = new Map<number, string>()
    for (const group of groups.data?.groups ?? []) names.set(group.id, group.name)
    return { names, groups: groups.data?.groups ?? [], canList }
  }, [groups.data, canList])
}

// Per-vault size, quota and backend status in one read (admin only: it is a system-wide stat).
export const useStorageOverview = () => {
  const isAdmin = useIsAdmin()
  return useWs('stats.system.storage', null, { enabled: isAdmin, retry: false, refetchInterval: 30_000 })
}

export const VAULT_EDIT = { anyOf: ['admin.vaults.self.edit', 'admin.vaults.user.edit', 'admin.vaults.admin.edit'] }
export const VAULT_REMOVE = { anyOf: ['admin.vaults.self.remove', 'admin.vaults.user.remove', 'admin.vaults.admin.remove'] }
export const VAULT_CREATE = { anyOf: ['admin.vaults.self.create', 'admin.vaults.user.create', 'admin.vaults.admin.create'] }
