export const userRoleMap: Record<string, string> = {
  super_admin: 'Super Administrator',
  admin: 'Administrator',
  user: 'User',
  guest: 'Guest',
  moderator: 'Moderator',
}

export const userRoleOptions = Object.entries(userRoleMap).map(([value, label]) => ({ value, label }))

export const displayNameToRole = Object.fromEntries(Object.entries(userRoleMap).map(([key, val]) => [val, key]))
