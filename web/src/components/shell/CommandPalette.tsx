'use client'

import React from 'react'
import { useRouter } from 'next/navigation'
import { Command } from 'cmdk'
import * as RD from '@radix-ui/react-dialog'
import { useVisibleNav } from '@/components/shell/useVisibleNav'
import { useCommandPalette } from '@/components/shell/commandPaletteStore'
import { useWs } from '@/lib/query'
import { useCan } from '@/lib/permissions'
import { logout } from '@/lib/session'
import { FolderIcon, MagnifyingGlassIcon, PlusIcon, RightFromBracketIcon, UserIcon, VaultIcon } from '@/components/ui/icons'

const itemClass =
  'flex cursor-default items-center gap-3 rounded-md px-3 py-2 text-sm text-fg-muted data-[selected=true]:bg-surface-3 data-[selected=true]:text-fg [&_svg]:size-4 [&_svg]:text-fg-subtle data-[selected=true]:[&_svg]:text-accent-text'
const groupClass = '[&_[cmdk-group-heading]]:px-3 [&_[cmdk-group-heading]]:pt-3 [&_[cmdk-group-heading]]:pb-1.5 [&_[cmdk-group-heading]]:text-[11px] [&_[cmdk-group-heading]]:font-medium [&_[cmdk-group-heading]]:tracking-wider [&_[cmdk-group-heading]]:text-fg-faint [&_[cmdk-group-heading]]:uppercase'

const Vaults = ({ go }: { go: (href: string) => void }) => {
  const canManage = useCan({ anyOf: ['admin.vaults.self.view', 'admin.vaults.user.view', 'admin.vaults.admin.view'] })
  const vaults = useWs('storage.vault.list', null, { staleTime: 60_000 })
  const list = vaults.data?.vaults ?? []
  if (!list.length) return null
  return (
    <Command.Group heading="Vaults" className={groupClass}>
      {list.map(vault => (
        <React.Fragment key={vault.id}>
          <Command.Item value={`files ${vault.name}`} onSelect={() => go(`/files/${vault.id}`)} className={itemClass}>
            <FolderIcon aria-hidden />
            Browse {vault.name}
          </Command.Item>
          {canManage ? (
            <Command.Item value={`vault ${vault.name} settings`} onSelect={() => go(`/vaults/${vault.id}`)} className={itemClass}>
              <VaultIcon aria-hidden />
              {vault.name}
              <span className="ml-auto text-xs text-fg-faint">vault</span>
            </Command.Item>
          ) : null}
        </React.Fragment>
      ))}
    </Command.Group>
  )
}

const Users = ({ go }: { go: (href: string) => void }) => {
  const users = useWs('auth.users.list', null, { staleTime: 60_000 })
  const list = users.data?.users ?? []
  if (!list.length) return null
  return (
    <Command.Group heading="Users" className={groupClass}>
      {list.map(user => (
        <Command.Item key={user.id} value={`user ${user.name} ${user.email ?? ''}`} onSelect={() => go(`/users/${encodeURIComponent(user.name)}`)} className={itemClass}>
          <UserIcon aria-hidden />
          {user.name}
        </Command.Item>
      ))}
    </Command.Group>
  )
}

export const CommandPalette = () => {
  const open = useCommandPalette(state => state.open)
  const setOpen = useCommandPalette(state => state.setOpen)
  const router = useRouter()
  const sections = useVisibleNav()
  const canUsers = useCan({ permission: 'admin.identities.users.view' })
  const canCreateVault = useCan({ anyOf: ['admin.vaults.self.create', 'admin.vaults.user.create', 'admin.vaults.admin.create'] })

  const go = (href: string) => {
    setOpen(false)
    router.push(href)
  }

  return (
    <RD.Root open={open} onOpenChange={setOpen}>
      <RD.Portal>
        <RD.Overlay className="fixed inset-0 z-[60] animate-fade-in bg-black/55 backdrop-blur-[2px]" />
        <RD.Content
          aria-describedby={undefined}
          className="glass-strong fixed top-[12vh] left-1/2 z-[61] w-[calc(100vw-2rem)] max-w-xl -translate-x-1/2 animate-pop-in overflow-hidden rounded-panel focus:outline-none">
          <RD.Title className="sr-only">Search and commands</RD.Title>
          <Command label="Search and commands" loop>
            <div className="flex items-center gap-3 border-b border-line px-4">
              <MagnifyingGlassIcon className="size-4 text-fg-subtle" aria-hidden />
              <Command.Input
                autoFocus
                placeholder="Search pages, vaults, users, actions…"
                className="h-12 flex-1 bg-transparent text-[15px] text-fg placeholder:text-fg-faint focus:outline-none"
              />
            </div>
            <Command.List className="max-h-[min(60vh,440px)] overflow-y-auto p-1.5 scrollbar-thin">
              <Command.Empty className="px-3 py-8 text-center text-sm text-fg-subtle">No matches.</Command.Empty>
              <Command.Group heading="Go to" className={groupClass}>
                {sections.flatMap(section =>
                  section.items.map(item => (
                    <Command.Item key={item.href} value={`${item.label} ${item.keywords ?? ''}`} onSelect={() => go(item.href)} className={itemClass}>
                      <item.icon aria-hidden />
                      {item.label}
                      {section.label ? <span className="ml-auto text-xs text-fg-faint">{section.label}</span> : null}
                    </Command.Item>
                  )),
                )}
              </Command.Group>
              <Command.Group heading="Actions" className={groupClass}>
                {canCreateVault ? (
                  <Command.Item value="new vault create" onSelect={() => go('/vaults/new')} className={itemClass}>
                    <PlusIcon aria-hidden />
                    New vault
                  </Command.Item>
                ) : null}
                {canUsers ? (
                  <Command.Item value="new user add invite" onSelect={() => go('/users/new')} className={itemClass}>
                    <PlusIcon aria-hidden />
                    New user
                  </Command.Item>
                ) : null}
                <Command.Item value="account profile password" onSelect={() => go('/account')} className={itemClass}>
                  <UserIcon aria-hidden />
                  Your account
                </Command.Item>
                <Command.Item value="log out sign out" onSelect={() => void logout()} className={itemClass}>
                  <RightFromBracketIcon aria-hidden />
                  Log out
                </Command.Item>
              </Command.Group>
              {open ? <Vaults go={go} /> : null}
              {open && canUsers ? <Users go={go} /> : null}
            </Command.List>
          </Command>
        </RD.Content>
      </RD.Portal>
    </RD.Root>
  )
}
