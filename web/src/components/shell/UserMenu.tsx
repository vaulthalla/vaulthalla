'use client'

import React from 'react'
import Link from 'next/link'
import { useSession, logout } from '@/lib/session'
import { MenuContent, MenuItem, MenuPortal, MenuRoot, MenuSeparator, MenuTrigger, menuContentClass } from '@/components/ui/Menu'
import { RightFromBracketIcon, UserIcon } from '@/components/ui/icons'
import { titleCase } from '@/lib/format'

export const Avatar = ({ name, className = '' }: { name: string; className?: string }) => (
  <span
    aria-hidden
    className={`grid size-8 shrink-0 place-items-center rounded-full border border-accent-line bg-accent-soft text-[13px] font-semibold text-accent-text uppercase ${className}`}>
    {name.slice(0, 1)}
  </span>
)

export const UserMenu = () => {
  const user = useSession(state => state.user)
  if (!user) return null
  return (
    <MenuRoot modal={false}>
      <MenuTrigger asChild>
        <button type="button" aria-label={`Account menu for ${user.name}`} className="rounded-full p-0.5 transition-shadow hover:shadow-[0_0_0_2px_var(--accent-line)]">
          <Avatar name={user.name} />
        </button>
      </MenuTrigger>
      <MenuPortal>
        <MenuContent align="end" sideOffset={8} className={`${menuContentClass} w-64`}>
          <div className="flex items-center gap-3 px-2.5 py-2">
            <Avatar name={user.name} />
            <div className="min-w-0">
              <div className="truncate text-sm font-medium text-fg" title={user.name}>
                {user.name}
              </div>
              <div className="truncate text-xs text-fg-subtle">{user.admin_role?.name ? titleCase(user.admin_role.name) : 'User'}</div>
            </div>
          </div>
          <MenuSeparator />
          <MenuItem asChild>
            <Link href="/account">
              <UserIcon aria-hidden />
              Your account
            </Link>
          </MenuItem>
          <MenuSeparator />
          <MenuItem onSelect={() => void logout()}>
            <RightFromBracketIcon aria-hidden />
            Log out
          </MenuItem>
        </MenuContent>
      </MenuPortal>
    </MenuRoot>
  )
}
