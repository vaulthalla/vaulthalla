// Client mirror of core's account-management rules (core/src/ops/Users.cpp, core/src/ops/Roles.cpp). The server
// enforces every one of these; the console uses them to disable a control and say why, instead of letting the
// server refuse after the fact.

import type { IUser } from '@/models/user'
import { isSuperAdminUser } from '@/lib/permissions'
import type { AdminRoleRecord, PermissionRecord, UserRecord } from '@/features/access/types'

export const SUPER_ADMIN_ROLE = 'super_admin'

// Seeded roles (core include/rbac/role/{Admin,Vault}.hpp and the share presets from deploy/psql/071).
export const BUILTIN_ADMIN_ROLES = new Set([
  'unprivileged',
  'auditor',
  'support',
  'identity_admin',
  'security_admin',
  'platform_operator',
  'vault_admin',
  'admin',
  'super_admin',
  'key_custodian',
])
export const BUILTIN_VAULT_ROLES = new Set([
  'implicit_deny',
  'guest',
  'reader',
  'contributor',
  'editor',
  'manager',
  'power_user',
  'full',
  'role_manager',
  'sync_operator',
  'share_preview_read',
  'share_download_only',
  'share_upload_dropbox',
  'share_browse_download',
  'share_contributor_scoped',
])

export const isBuiltinRole = (type: 'admin' | 'vault', name: string) =>
  (type === 'admin' ? BUILTIN_ADMIN_ROLES : BUILTIN_VAULT_ROLES).has(name)

type Actor = IUser | null

const granted = (permissions: PermissionRecord[] | undefined) => {
  const set = new Set<string>()
  for (const p of permissions ?? []) if (p.value) set.add(p.qualified)
  return set
}

export const actorPermissions = (actor: Actor) =>
  granted((actor?.admin_role?.permissions ?? []) as unknown as PermissionRecord[])

// core ops::users::isAdminIdentity: any admin permission beyond the account's own vaults and own API keys makes the
// account an "admin account", which needs admin.identities.admins.* to manage.
export const isAdminIdentityRole = (role: AdminRoleRecord | null | undefined): boolean | null => {
  if (!role?.permissions) return null
  return role.permissions.some(
    p => p.value && !p.qualified.startsWith('admin.vaults.self.') && !p.qualified.startsWith('admin.keys.api.self.'),
  )
}

// Admin permissions this role grants that the actor lacks (core roles::permissionsBeyondActor).
export const permissionsBeyondActor = (actor: Actor, role: AdminRoleRecord | null | undefined): string[] | null => {
  if (!role?.permissions) return null
  if (isSuperAdminUser(actor)) return []
  const mine = actorPermissions(actor)
  return role.permissions.filter(p => p.value && !mine.has(p.qualified)).map(p => p.qualified)
}

export type IdentityVerb = 'view' | 'add' | 'edit' | 'delete' | 'reset-password'

// Whether the actor holds the identity permission for an account (or role) of this class. Unknown class (a slim role)
// → true when either class is permitted, so the UI never hides what the server might allow.
export const canForClass = (actor: Actor, verb: IdentityVerb, role: AdminRoleRecord | null | undefined) => {
  const mine = actorPermissions(actor)
  const users = mine.has(`admin.identities.users.${verb}`)
  const admins = mine.has(`admin.identities.admins.${verb}`)
  const adminClass = isAdminIdentityRole(role)
  if (adminClass === null) return users || admins
  return adminClass ? admins : users
}

export interface Verdict {
  allowed: boolean
  reason?: string
}

const ok: Verdict = { allowed: true }
const no = (reason: string): Verdict => ({ allowed: false, reason })

export const isSuperAdminAccount = (user: Pick<UserRecord, 'admin_role'>) => user.admin_role?.name === SUPER_ADMIN_ROLE

// ops::users::requireManage, as far as the client can know it (the role ceiling needs the target role's permissions).
export const canManageAccount = (actor: Actor, target: UserRecord, verb: Exclude<IdentityVerb, 'view' | 'add'>, role?: AdminRoleRecord | null): Verdict => {
  if (target.is_protected) return no('This is a protected account. It can only be changed from the server with the vh CLI.')
  if (isSuperAdminAccount(target)) return no('Super admin accounts can’t be changed from another account.')
  const effectiveRole = role ?? target.admin_role
  if (!canForClass(actor, verb, effectiveRole)) {
    const kind = isAdminIdentityRole(effectiveRole) ? 'admin accounts' : 'user accounts'
    const action = verb === 'reset-password' ? 'reset passwords of' : verb === 'delete' ? 'delete' : 'edit'
    return no(`Your role doesn’t allow you to ${action} ${kind}.`)
  }
  const beyond = permissionsBeyondActor(actor, effectiveRole)
  if (beyond && beyond.length)
    return no('This account’s role grants admin permissions you don’t hold, so only a more privileged administrator can change it.')
  return ok
}

export interface AccountRules {
  self: boolean
  profile: Verdict
  rename: Verdict
  role: Verdict
  active: Verdict
  password: Verdict
  remove: Verdict
}

// Everything the user detail page needs to decide which controls are live.
export const accountRules = (actor: Actor, target: UserRecord, role?: AdminRoleRecord | null): AccountRules => {
  const self = actor?.id === target.id
  if (self) {
    const profile = target.is_protected
      ? no('This is a protected account. It can only be changed from the server with the vh CLI.')
      : ok
    return {
      self,
      profile,
      rename: !profile.allowed ? profile : isSuperAdminAccount(target) ? no('The super admin account can’t be renamed.') : ok,
      role: no('You can’t change your own role. Ask another administrator.'),
      active: no('You can’t deactivate your own account.'),
      password: { allowed: true, reason: 'Change your own password from your account page.' },
      remove: no('You can’t delete your own account. Ask another administrator.'),
    }
  }
  const edit = canManageAccount(actor, target, 'edit', role)
  return {
    self,
    profile: edit,
    rename: edit,
    role: edit,
    active: edit,
    password: canManageAccount(actor, target, 'reset-password', role),
    remove: canManageAccount(actor, target, 'delete', role),
  }
}

// Whether the actor may assign this role (ops::users::requireAssignable + the class permission).
export const canAssignRole = (actor: Actor, role: AdminRoleRecord, verb: 'add' | 'edit'): Verdict => {
  if (role.name === SUPER_ADMIN_ROLE) return no('The super_admin role can’t be assigned.')
  if (!canForClass(actor, verb, role))
    return no(`Your role doesn’t allow you to ${verb === 'add' ? 'create' : 'manage'} ${isAdminIdentityRole(role) ? 'admin' : 'user'} accounts.`)
  const beyond = permissionsBeyondActor(actor, role)
  if (beyond && beyond.length) return no('Grants admin permissions you don’t hold.')
  return ok
}

// Role-editing rules (ops::roles::updateAdminRole / removeAdminRole).
export const roleEditRules = (actor: Actor, type: 'admin' | 'vault', role: { name: string } | null) => {
  const mine = actorPermissions(actor)
  const canEdit = mine.has(`admin.roles.${type}.edit`)
  const canDelete = mine.has(`admin.roles.${type}.delete`)
  if (type === 'admin' && role) {
    if (role.name === SUPER_ADMIN_ROLE) {
      const reason = 'The built-in super_admin role always holds every permission and can’t be changed.'
      return { edit: no(reason), remove: no(reason) }
    }
    if (actor?.admin_role?.name === role.name) {
      const reason = 'This is your own role. Ask another administrator to change it.'
      return { edit: no(reason), remove: no(reason) }
    }
  }
  return {
    edit: canEdit ? ok : no(`Your role doesn’t allow you to edit ${type} roles.`),
    remove: canDelete ? ok : no(`Your role doesn’t allow you to delete ${type} roles.`),
  }
}
