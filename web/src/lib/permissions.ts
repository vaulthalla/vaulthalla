'use client'

import { useSession } from '@/lib/session'
import type { IUser } from '@/models/user'

// UI gating only. The server enforces every permission; this decides what to offer, never what is allowed.

const adminPermissions = (user: IUser | null): Set<string> => {
  const set = new Set<string>()
  for (const permission of user?.admin_role?.permissions ?? []) if (permission.value) set.add(permission.qualified)
  return set
}

// Mirrors core User::isAdmin(): the admin role can delete admins and remove admin vaults.
export const isAdminUser = (user: IUser | null) => {
  const perms = adminPermissions(user)
  return perms.has('admin.identities.admins.delete') && perms.has('admin.vaults.admin.remove')
}

export const isSuperAdminUser = (user: IUser | null) => isAdminUser(user) && user?.admin_role?.name === 'super_admin'

export const hasAdminPermission = (user: IUser | null, qualified: string) => adminPermissions(user).has(qualified)

// Any admin permission under a prefix, e.g. 'admin.identities.users'.
export const hasAnyAdminPermission = (user: IUser | null, prefix: string) => {
  for (const qualified of adminPermissions(user)) if (qualified === prefix || qualified.startsWith(`${prefix}.`)) return true
  return false
}

export type Requirement =
  | { admin: true }
  | { permission: string }
  | { anyOf: string[] }
  | { prefix: string }

export const meets = (user: IUser | null, requirement?: Requirement): boolean => {
  if (!requirement) return true
  if ('admin' in requirement) return isAdminUser(user)
  if ('permission' in requirement) return hasAdminPermission(user, requirement.permission)
  if ('anyOf' in requirement) return requirement.anyOf.some(p => hasAdminPermission(user, p))
  return hasAnyAdminPermission(user, requirement.prefix)
}

export const useCan = (requirement?: Requirement) => meets(useSession(state => state.user), requirement)

export const useIsAdmin = () => isAdminUser(useSession(state => state.user))
