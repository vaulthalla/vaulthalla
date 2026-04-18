'use client'

import React, { useEffect, useRef } from 'react'
import { createPortal } from 'react-dom'
import TrashIcon from '@/fa-duotone/trash.svg'
import EditIcon from '@/fa-duotone/pen-to-square.svg'
import CopyIcon from '@/fa-duotone/copy.svg'
import PasteIcon from '@/fa-duotone/paste.svg'
import { useFSStore } from '@/stores/fsStore'

interface ContextMenuProps<T extends { name: string }> {
  data: T
  position: { x: number; y: number }
  onClose: () => void
  onDelete: (item: T) => void
  onCopy?: (item: T) => void
  onRename?: (item: T) => void
}

export function ContextMenu<
  T extends { name: string; path?: string; file_count?: number; subdirectory_count?: number },
>({ data, position, onClose, onDelete, onCopy, onRename }: ContextMenuProps<T>) {
  const menuRef = useRef<HTMLDivElement>(null)

  const { copiedItem, pasteCopiedItem } = useFSStore()

  const isDirectory = typeof data.file_count === 'number' || typeof data.subdirectory_count === 'number'
  const canPaste = copiedItem && isDirectory

  useEffect(() => {
    function handleClickOutside(e: MouseEvent) {
      if (!menuRef.current?.contains(e.target as Node)) {
        onClose()
      }
    }
    document.addEventListener('mousedown', handleClickOutside)
    return () => document.removeEventListener('mousedown', handleClickOutside)
  }, [onClose])

  useEffect(() => {
    menuRef.current?.focus()
  }, [])

  const ContextButton = ({ children, onClick }: { children: React.ReactNode; onClick?: () => void }) => (
    <button
      className="inline-flex gap-x-2 rounded-xl p-1 transition-transform hover:scale-105 hover:bg-black/30"
      onClick={e => {
        e.stopPropagation()
        onClick?.()
        onClose()
      }}>
      {children}
    </button>
  )

  return createPortal(
    <div className="fixed inset-0 z-50">
      <div
        ref={menuRef}
        tabIndex={-1}
        style={{ top: position.y, left: position.x }}
        className="absolute rounded border border-gray-600 bg-gray-800 p-2 shadow-lg focus:outline-none">
        <div className="flex flex-col gap-1 text-lg">
          <ContextButton onClick={() => onRename?.(data)}>
            <EditIcon className="text-glow-orange my-1 fill-current" />
            Rename
          </ContextButton>
          <ContextButton onClick={() => onCopy?.(data)}>
            <CopyIcon className="text-primary my-1 fill-current" />
            Copy
          </ContextButton>
          <ContextButton onClick={() => onDelete(data)}>
            <TrashIcon className="text-destructive my-1 fill-current" />
            Delete
          </ContextButton>

          {canPaste && copiedItem && (
            <ContextButton onClick={() => pasteCopiedItem(`${data.path ?? data.name}/${copiedItem.name}`)}>
              <PasteIcon className="my-1 fill-current text-green-400" />
              Paste
            </ContextButton>
          )}
        </div>
      </div>
    </div>,
    document.body,
  )
}
