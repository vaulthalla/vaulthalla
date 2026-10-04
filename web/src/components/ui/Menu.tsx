'use client'

import React, { Suspense, lazy, useState } from 'react'

// One menu vocabulary for dropdowns (a visible "⋯" or button) and context menus (right click / context-menu key).
// The Radix implementation loads on first interaction (and is prefetched on hover), keeping it off first load.
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

const loadImpl = () => import('@/components/ui/MenuImpl')
const DropdownImpl = lazy(loadImpl)
const AnchoredMenu = lazy(() => loadImpl().then(m => ({ default: m.AnchoredMenu })))

type TriggerProps = React.HTMLAttributes<HTMLElement> & { 'aria-haspopup'?: 'menu' }

export const DropdownMenu = ({
  trigger,
  entries,
  align = 'end',
  label,
  header,
  className,
}: {
  trigger: React.ReactElement<TriggerProps>
  entries: MenuEntry[]
  align?: 'start' | 'center' | 'end'
  label?: string
  header?: React.ReactNode
  className?: string
}) => {
  const [armed, setArmed] = useState(false)
  if (!armed) {
    const props = trigger.props
    return React.cloneElement(trigger, {
      'aria-haspopup': 'menu',
      onPointerEnter: (event: React.PointerEvent<HTMLElement>) => {
        void loadImpl()
        props.onPointerEnter?.(event)
      },
      onPointerDown: (event: React.PointerEvent<HTMLElement>) => {
        if (event.button !== 0) return
        event.preventDefault()
        setArmed(true)
      },
      onKeyDown: (event: React.KeyboardEvent<HTMLElement>) => {
        if (event.key === 'Enter' || event.key === ' ' || event.key === 'ArrowDown') {
          event.preventDefault()
          setArmed(true)
        }
        props.onKeyDown?.(event)
      },
    })
  }
  return (
    <Suspense fallback={trigger}>
      <DropdownImpl trigger={trigger} entries={entries} align={align} label={label} header={header} className={className} />
    </Suspense>
  )
}

// Wraps a region; right click (or the context-menu key / Shift+F10) opens `entries()` at the pointer.
export const ContextMenu = ({
  children,
  entries,
  onOpenChange,
}: {
  children: React.ReactElement<React.HTMLAttributes<HTMLElement>>
  entries: MenuEntry[]
  onOpenChange?: (open: boolean) => void
}) => {
  const [at, setAt] = useState<{ x: number; y: number } | null>(null)
  const child = React.cloneElement(children, {
    onPointerEnter: (event: React.PointerEvent<HTMLElement>) => {
      void loadImpl()
      children.props.onPointerEnter?.(event)
    },
    onContextMenu: (event: React.MouseEvent<HTMLElement>) => {
      children.props.onContextMenu?.(event)
      event.preventDefault()
      // Keyboard-triggered context menus report (0,0): anchor to the focused element instead.
      let { clientX: x, clientY: y } = event
      if (!x && !y) {
        const rect = (document.activeElement ?? event.currentTarget).getBoundingClientRect()
        x = rect.left + 24
        y = rect.top + rect.height / 2
      }
      setAt({ x, y })
      onOpenChange?.(true)
    },
  })
  return (
    <>
      {child}
      {at && entries.length ? (
        <Suspense fallback={null}>
          <AnchoredMenu
            x={at.x}
            y={at.y}
            entries={entries}
            onClose={() => {
              setAt(null)
              onOpenChange?.(false)
            }}
          />
        </Suspense>
      ) : null}
    </>
  )
}
