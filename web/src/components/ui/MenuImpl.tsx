'use client'

// Radix menu implementation. Loaded on first interaction by Menu.tsx so no page pays for menus up front.
import React, { useEffect, useRef } from 'react'
import * as DM from '@radix-ui/react-dropdown-menu'
import { cn } from '@/util/cn'
import type { MenuEntry } from '@/components/ui/Menu'

const itemClass =
  'flex cursor-default select-none items-center gap-2.5 rounded-md px-2.5 py-1.5 text-sm text-fg-muted outline-none data-[disabled]:pointer-events-none data-[disabled]:opacity-40 data-[highlighted]:bg-surface-3 data-[highlighted]:text-fg [&_svg]:size-4 [&_svg]:text-fg-subtle data-[highlighted]:[&_svg]:text-accent-text'
export const contentClass = 'glass-strong z-[65] min-w-48 animate-pop-in rounded-card p-1.5'

const Entries = ({ entries }: { entries: MenuEntry[] }) => (
  <>
    {entries.map((entry, index) =>
      entry === 'separator' ? (
        <DM.Separator key={`sep-${index}`} className="my-1 h-px bg-line" />
      ) : (
        <DM.Item
          key={entry.key}
          disabled={entry.disabled}
          onSelect={entry.onSelect}
          className={cn(itemClass, entry.danger && 'text-danger data-[highlighted]:bg-danger-soft data-[highlighted]:text-danger [&_svg]:text-danger/80')}>
          {entry.icon ? <entry.icon aria-hidden /> : null}
          <span className="flex-1">{entry.label}</span>
          {entry.shortcut ? <span className="font-mono text-[11px] text-fg-faint">{entry.shortcut}</span> : null}
        </DM.Item>
      ),
    )}
  </>
)

export interface DropdownImplProps {
  trigger: React.ReactElement
  entries: MenuEntry[]
  align?: 'start' | 'center' | 'end'
  label?: string
  header?: React.ReactNode
  className?: string
}

export default function DropdownImpl({ trigger, entries, align = 'end', label, header, className }: DropdownImplProps) {
  return (
    <DM.Root modal={false} defaultOpen>
      <DM.Trigger asChild>{trigger}</DM.Trigger>
      <DM.Portal>
        <DM.Content align={align} sideOffset={6} collisionPadding={8} className={cn(contentClass, className)} aria-label={label}>
          {header ? (
            <>
              {header}
              <DM.Separator className="my-1 h-px bg-line" />
            </>
          ) : null}
          <Entries entries={entries} />
        </DM.Content>
      </DM.Portal>
    </DM.Root>
  )
}

// A menu opened at a point (right click / long press / context-menu key), anchored to an invisible fixed element.
export function AnchoredMenu({ x, y, entries, onClose }: { x: number; y: number; entries: MenuEntry[]; onClose: () => void }) {
  const anchor = useRef<HTMLSpanElement>(null)
  useEffect(() => {
    anchor.current?.focus()
  }, [])
  return (
    <DM.Root open modal={false} onOpenChange={open => !open && onClose()}>
      <DM.Trigger asChild>
        <span ref={anchor} tabIndex={-1} aria-hidden className="pointer-events-none fixed size-px" style={{ left: x, top: y }} />
      </DM.Trigger>
      <DM.Portal>
        <DM.Content align="start" side="bottom" sideOffset={2} collisionPadding={8} className={contentClass} onCloseAutoFocus={event => event.preventDefault()}>
          <Entries entries={entries} />
        </DM.Content>
      </DM.Portal>
    </DM.Root>
  )
}
