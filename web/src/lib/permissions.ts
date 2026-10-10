'use client'

import { useSession } from '@/lib/session'
import type { IUser } from '@/models/user'

// UI gating only. The server enforces every permission; this decides what to offer, never what is allowed.

const adminPermissions = (user: IUser | null): Set<string> => {
  const set = new Set<string>()
  for (const permission of user?.admin_role?.permissions ?? []) if (permission.value) set.add(permission.qualified)
  return set
}

// Mirrors core User::isAdmin(): the admin role can delete admins and remove admin vaults. Core keeps that strict
// "full admin" test for S3 policy bypass and the RBAC resolvers; don't gate new UI on it, gate on the permission the
// server checks (e.g. Health/stats on admin.stats.view, #166).
export const isAdminUser = (user: IUser | null) => {
  const perms = adminPermissions(user)
  return perms.has('admin.identities.admins.delete') && perms.has('admin.vaults.admin.remove')
}

// Mirrors core User::isSuperAdmin().
export const isSuperAdminUser = (user: IUser | null) =>
  isAdminUser(user) && hasAdminPermission(user, 'admin.keys.encryption.rotate') && user?.admin_role?.name === 'super_admin'

export const hasAdminPermission = (user: IUser | null, qualified: string) => adminPermissions(user).has(qualified)

// Server, daemon and system stats (Health, the top-bar health dot, system-wide storage sizes). Mirrors core
// ops::stats::canViewSystem. A vault's own stats are authorized per vault by the server (its owner, or
// admin.vaults.*.view + view_stats), so vault pages just ask and render the typed denial.
export const STATS_VIEW = { permission: 'admin.stats.view' } as const

// Any admin permission under a prefix, e.g. 'admin.identities.users'.
export const hasAnyAdminPermission = (user: IUser | null, prefix: string) => {
  for (const qualified of adminPermissions(user)) if (qualified === prefix || qualified.startsWith(`${prefix}.`)) return true
  return false
}

export type Requirement =
  | { superAdmin: true }
  | { permission: string }
  | { anyOf: string[] }
  | { prefix: string }

export const meets = (user: IUser | null, requirement?: Requirement): boolean => {
  if (!requirement) return true
  if ('superAdmin' in requirement) return isSuperAdminUser(user)
  if ('permission' in requirement) return hasAdminPermission(user, requirement.permission)
  if ('anyOf' in requirement) return requirement.anyOf.some(p => hasAdminPermission(user, p))
  return hasAnyAdminPermission(user, requirement.prefix)
}

export const useCan = (requirement?: Requirement) => meets(useSession(state => state.user), requirement)

export const useIsAdmin = () => isAdminUser(useSession(state => state.user))
