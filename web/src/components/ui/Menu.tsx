'use client'

import React from 'react'
import * as DM from '@radix-ui/react-dropdown-menu'
import * as CM from '@radix-ui/react-context-menu'
import { cn } from '@/util/cn'

// One menu vocabulary for dropdowns (a visible "⋯" or button) and context menus (right click / long press).
export interface MenuAction {
  key: string
  label: string
  icon?: React.ComponentType<React.SVGProps<SVGSVGElement>>
  onSelect: () => void
  danger?: boolean
  disabled?: boolean
  shortcut?: string
}

export type MenuEntry = MenuAction | 'separator'

const itemClass =
  'flex cursor-default select-none items-center gap-2.5 rounded-md px-2.5 py-1.5 text-sm text-fg-muted outline-none data-[disabled]:pointer-events-none data-[disabled]:opacity-40 data-[highlighted]:bg-surface-3 data-[highlighted]:text-fg [&_svg]:size-4 [&_svg]:text-fg-subtle data-[highlighted]:[&_svg]:text-accent-text'
const contentClass = 'glass-strong z-[65] min-w-48 animate-pop-in rounded-card p-1.5'

const renderEntries = (entries: MenuEntry[], Item: typeof DM.Item | typeof CM.Item, Separator: typeof DM.Separator | typeof CM.Separator) =>
  entries.map((entry, index) =>
    entry === 'separator' ? (
      <Separator key={`sep-${index}`} className="my-1 h-px bg-line" />
    ) : (
      <Item
        key={entry.key}
        disabled={entry.disabled}
        onSelect={entry.onSelect}
        className={cn(itemClass, entry.danger && 'text-danger data-[highlighted]:bg-danger-soft data-[highlighted]:text-danger [&_svg]:text-danger/80')}>
        {entry.icon ? <entry.icon aria-hidden /> : null}
        <span className="flex-1">{entry.label}</span>
        {entry.shortcut ? <span className="font-mono text-[11px] text-fg-faint">{entry.shortcut}</span> : null}
      </Item>
    ),
  )

export const DropdownMenu = ({
  trigger,
  entries,
  align = 'end',
  label,
}: {
  trigger: React.ReactNode
  entries: MenuEntry[]
  align?: 'start' | 'center' | 'end'
  label?: string
}) => (
  <DM.Root modal={false}>
    <DM.Trigger asChild>{trigger}</DM.Trigger>
    <DM.Portal>
      <DM.Content align={align} sideOffset={6} collisionPadding={8} className={contentClass} aria-label={label}>
        {renderEntries(entries, DM.Item, DM.Separator)}
      </DM.Content>
    </DM.Portal>
  </DM.Root>
)

export const ContextMenu = ({
  children,
  entries,
  onOpenChange,
}: {
  children: React.ReactNode
  entries: MenuEntry[]
  onOpenChange?: (open: boolean) => void
}) => (
  <CM.Root modal={false} onOpenChange={onOpenChange}>
    <CM.Trigger asChild>{children}</CM.Trigger>
    <CM.Portal>
      <CM.Content collisionPadding={8} className={contentClass}>
        {renderEntries(entries, CM.Item, CM.Separator)}
      </CM.Content>
    </CM.Portal>
  </CM.Root>
)

// Arbitrary content in a dropdown panel (e.g. the user menu header).
export const MenuContent = DM.Content
export const MenuRoot = DM.Root
export const MenuTrigger = DM.Trigger
export const MenuPortal = DM.Portal
export const MenuItem = ({ className, ...props }: React.ComponentPropsWithoutRef<typeof DM.Item>) => (
  <DM.Item className={cn(itemClass, className)} {...props} />
)
export const MenuSeparator = () => <DM.Separator className="my-1 h-px bg-line" />
export const menuContentClass = contentClass
