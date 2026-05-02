'use client'

import React, { useEffect, useRef } from 'react'
import { createPortal } from 'react-dom'
import TrashIcon from '@/fa-duotone/trash.svg'
import EditIcon from '@/fa-duotone/pen-to-square.svg'
import CopyIcon from '@/fa-duotone/copy.svg'
import PasteIcon from '@/fa-duotone/paste.svg'
import ShareIcon from '@/fa-duotone/share-nodes.svg'
import DownloadIcon from '@/fa-duotone/download.svg'
import { useFSStore } from '@/stores/fsStore'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { hasShareOperation } from '@/util/shareOperations'

interface ContextMenuProps<T extends { name: string }> {
  data: T
  position: { x: number; y: number }
  onClose: () => void
  onDelete: (item: T) => void
  onCopy?: (item: T) => void
  onRename?: (item: T) => void
  onShare?: (item: T) => void
  onOpen?: (item: T) => void
  onPreview?: (item: T) => void
  onDownload?: (item: T) => void
}

export function ContextMenu<
  T extends { name: string; path?: string; file_count?: number; subdirectory_count?: number; mime_type?: string },
>({ data, position, onClose, onDelete, onCopy, onRename, onShare, onOpen, onPreview, onDownload }: ContextMenuProps<T>) {
  const menuRef = useRef<HTMLDivElement>(null)

  const copiedItem = useFSStore(state => state.copiedItem)
  const pasteCopiedItem = useFSStore(state => state.pasteCopiedItem)
  const mode = useFSStore(state => state.mode)
  const share = useVaultShareStore(state => state.share)

  const isDirectory = typeof data.file_count === 'number' || typeof data.subdirectory_count === 'number'
  const isShareMode = mode === 'share'
  const canPaste = !isShareMode && copiedItem && isDirectory
  const canPreview = hasShareOperation(share?.allowed_ops, 'preview')
  const canDownload = hasShareOperation(share?.allowed_ops, 'download')
  const isPreviewable = Boolean(data.mime_type?.startsWith('image/') || data.mime_type === 'application/pdf')

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
          {!isShareMode && (
            <>
              <ContextButton onClick={() => onRename?.(data)}>
                <EditIcon className="text-glow-orange my-1 fill-current" />
                Rename
              </ContextButton>
              <ContextButton onClick={() => onCopy?.(data)}>
                <CopyIcon className="text-primary my-1 fill-current" />
                Copy
              </ContextButton>
              {onShare && (
                <ContextButton onClick={() => onShare(data)}>
                  <ShareIcon className="text-primary my-1 fill-current" />
                  Share
                </ContextButton>
              )}
              <ContextButton onClick={() => onDownload?.(data)}>
                <DownloadIcon className="text-primary my-1 fill-current" />
                Download
              </ContextButton>
              <ContextButton onClick={() => onDelete(data)}>
                <TrashIcon className="text-destructive my-1 fill-current" />
                Delete
              </ContextButton>
            </>
          )}

          {canPaste && copiedItem && (
            <ContextButton onClick={() => pasteCopiedItem(`${data.path ?? data.name}/${copiedItem.name}`)}>
              <PasteIcon className="my-1 fill-current text-green-400" />
              Paste
            </ContextButton>
          )}

          {isShareMode && (
            <>
              {isDirectory && (
                <ContextButton onClick={() => onOpen?.(data)}>
                  <PasteIcon className="my-1 fill-current text-cyan-300" />
                  Open
                </ContextButton>
              )}
              {!isDirectory && canPreview && isPreviewable && (
                <ContextButton onClick={() => onPreview?.(data)}>
                  <CopyIcon className="text-primary my-1 fill-current" />
                  Preview
                </ContextButton>
              )}
              {canDownload && (
                <ContextButton onClick={() => onDownload?.(data)}>
                  <DownloadIcon className="text-primary my-1 fill-current" />
                  Download
                </ContextButton>
              )}
              {isDirectory || (canPreview && isPreviewable) || canDownload ? null : (
                <div className="px-2 py-1 text-sm text-gray-400">No actions available</div>
              )}
            </>
          )}
        </div>
      </div>
    </div>,
    document.body,
  )
}
