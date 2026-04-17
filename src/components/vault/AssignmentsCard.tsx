'use client'

import Link from 'next/link'
import { useEffect, useMemo, useState } from 'react'
import { Vault } from '@/models/vaults'
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'
import { VaultRole } from '@/models/role'
import { cn } from '@/util/cn'

const EMPTY_ASSIGNED_ROLES: VaultRole[] = []

type AssignmentsCardProps = {
  vault: Vault
  className?: string
}

export default function AssignmentsCard({ vault, className }: AssignmentsCardProps) {
  const [isLoading, setIsLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [isUsersOpen, setIsUsersOpen] = useState(false)
  const [isGroupsOpen, setIsGroupsOpen] = useState(false)

  const rolesForVault = useVaultRoleStore(state => state.assignedRolesByVault[vault.id])
  const assignedRoles = useMemo(() => rolesForVault ?? EMPTY_ASSIGNED_ROLES, [rolesForVault])

  useEffect(() => {
    let mounted = true

    const load = async () => {
      setIsLoading(true)
      setError(null)
      try {
        await useVaultRoleStore.getState().getAssignedRoles({ id: vault.id })
      } catch (e) {
        if (!mounted) return
        setError(e instanceof Error ? e.message : 'Failed to load assignments.')
      } finally {
        if (mounted) setIsLoading(false)
      }
    }

    void load()
    return () => {
      mounted = false
    }
  }, [vault.id])

  const assignedUserIds = useMemo(() => {
    const ids = assignedRoles
      .filter(role => role.subject_type === 'user' && role.subject_id != null)
      .map(role => role.subject_id as number)
    return [...new Set(ids)].sort((a, b) => a - b)
  }, [assignedRoles])

  const assignedGroupIds = useMemo(() => {
    const ids = assignedRoles
      .filter(role => role.subject_type === 'group' && role.subject_id != null)
      .map(role => role.subject_id as number)
    return [...new Set(ids)].sort((a, b) => a - b)
  }, [assignedRoles])

  return (
    <div className={cn('rounded-2xl border border-white/10 bg-white/5 p-4 text-left shadow', className)}>
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h3 className="text-base font-semibold text-white">Assignments</h3>
          <p className="mt-1 text-sm text-white/60">Manage who has this vault role assignment.</p>
        </div>

        <Link
          href={`/vaults/${vault.id}/assign`}
          className="inline-flex h-9 items-center justify-center rounded-md border border-cyan-400/30 bg-cyan-500/15 px-3 text-sm font-medium text-cyan-100 transition hover:bg-cyan-500/25">
          Assign User
        </Link>
      </div>

      <div className="mt-4 grid gap-2 sm:grid-cols-3">
        <div className="rounded-xl border border-white/10 bg-black/20 px-3 py-2">
          <div className="text-[10px] tracking-[0.14em] text-white/45 uppercase">Total</div>
          <div className="text-lg font-semibold text-white">{assignedRoles.length}</div>
        </div>
        <div className="rounded-xl border border-white/10 bg-black/20 px-3 py-2">
          <div className="text-[10px] tracking-[0.14em] text-white/45 uppercase">Users</div>
          <div className="text-lg font-semibold text-white">{assignedUserIds.length}</div>
        </div>
        <div className="rounded-xl border border-white/10 bg-black/20 px-3 py-2">
          <div className="text-[10px] tracking-[0.14em] text-white/45 uppercase">Groups</div>
          <div className="text-lg font-semibold text-white">{assignedGroupIds.length}</div>
        </div>
      </div>

      {isLoading && <div className="mt-3 text-sm text-white/55">Loading assignments...</div>}
      {error && <div className="mt-3 text-sm text-red-300">{error}</div>}

      <div className="mt-4 space-y-2">
        <button
          type="button"
          onClick={() => setIsUsersOpen(prev => !prev)}
          className="flex w-full items-center justify-between rounded-xl border border-white/10 bg-black/20 px-3 py-2 text-sm text-white/85 transition hover:bg-white/5">
          <span>Assigned Users</span>
          <span className="text-white/55">{assignedUserIds.length}</span>
        </button>

        {isUsersOpen && (
          <div className="space-y-1.5 rounded-xl border border-white/8 bg-black/20 p-2">
            {assignedUserIds.length ?
              assignedUserIds.map(userId => (
                <Link
                  key={userId}
                  href={`/users/${userId}`}
                  className="block rounded-lg border border-white/10 bg-white/5 px-3 py-2 text-sm text-white/85 transition hover:bg-white/10">
                  User #{userId}
                </Link>
              ))
            : <div className="px-2 py-1 text-sm text-white/50">No users assigned.</div>}
          </div>
        )}

        <button
          type="button"
          onClick={() => setIsGroupsOpen(prev => !prev)}
          className="flex w-full items-center justify-between rounded-xl border border-white/10 bg-black/20 px-3 py-2 text-sm text-white/85 transition hover:bg-white/5">
          <span>Assigned Groups</span>
          <span className="text-white/55">{assignedGroupIds.length}</span>
        </button>

        {isGroupsOpen && (
          <div className="space-y-1.5 rounded-xl border border-white/8 bg-black/20 p-2">
            {assignedGroupIds.length ?
              assignedGroupIds.map(groupId => (
                <div
                  key={groupId}
                  className="rounded-lg border border-white/10 bg-white/5 px-3 py-2 text-sm text-white/85">
                  Group #{groupId}
                </div>
              ))
            : <div className="px-2 py-1 text-sm text-white/50">No groups assigned.</div>}
          </div>
        )}
      </div>
    </div>
  )
}
