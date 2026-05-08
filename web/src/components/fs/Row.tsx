import Thumb from '@/components/fs/Thumb'
import PreviewThumb from '@/components/fs/PreviewThumb'
import ArrowRight from '@/fa-duotone/arrow-right.svg'
import Folder from '@/fa-duotone/folder.svg'
import FileIcon from '@/fa-duotone/file.svg'
import React from 'react'
import type { FilesystemRow, RowProps } from '@/components/fs/types'
import { toPreviewBatchCandidate } from '@/components/fs/previewBatch'

const rowMimeType = (row: FilesystemRow) => row.entryType === 'file' ? row.mime_type : undefined

const sameRow = (a: FilesystemRow, b: FilesystemRow) => (
  a === b ||
  (
    a.key === b.key &&
    a.id === b.id &&
    a.entryType === b.entryType &&
    a.vault_id === b.vault_id &&
    a.path === b.path &&
    a.name === b.name &&
    a.size === b.size &&
    a.size_bytes === b.size_bytes &&
    a.modified === b.modified &&
    a.updated_at === b.updated_at &&
    a.previewUrl === b.previewUrl &&
    rowMimeType(a) === rowMimeType(b)
  )
)

const Row = React.memo(function Row({ r, previewMode, onNavigate, onOpenFile, onContextMenu }: RowProps) {
  const previewCandidate = r.entryType === 'file' ? toPreviewBatchCandidate(r, previewMode) : null

  const click = React.useCallback(() => {
    if (r.entryType === 'directory') onNavigate(r.path ?? r.name)
    else onOpenFile(r)
  }, [r, onNavigate, onOpenFile])

  const handleCtx = React.useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault()
      onContextMenu(e, r)
    },
    [onContextMenu, r],
  )

  return (
    <tr
      className="group border-b border-gray-800/60 transition-colors hover:bg-gray-800/70"
      onContextMenu={handleCtx}
      onClick={click}>
      <td className="px-4 py-2.5 align-middle text-white">
        <div className="flex min-w-0 items-center gap-2">
          <div className="shrink-0">
            {r.entryType === 'file' && previewCandidate ?
              <PreviewThumb
                cacheKey={previewCandidate.key}
                signature={previewCandidate.signature}
                directSrc={previewCandidate.directUrl}
                alt={r.name}
              />
            : r.entryType === 'file' && r.previewUrl ?
              <Thumb src={r.previewUrl} alt={r.name} />
            : r.entryType === 'directory' ?
              <Folder className="text-primary fill-current" />
            : <FileIcon className="text-primary fill-current" />}
          </div>

          <span className="min-w-0 flex-1 truncate select-none">{r.name}</span>

          {r.entryType === 'directory' && (
            <ArrowRight className="text-primary h-4 w-4 shrink-0 opacity-0 transition-opacity group-hover:opacity-100" />
          )}
        </div>
      </td>

      <td className="px-4 py-2.5 align-middle whitespace-nowrap text-gray-200">{r.size}</td>
      <td className="px-4 py-2.5 align-middle whitespace-nowrap text-gray-300">{r.modified}</td>
    </tr>
  )
}, (prev, next) => (
  prev.previewMode === next.previewMode &&
  prev.onNavigate === next.onNavigate &&
  prev.onOpenFile === next.onOpenFile &&
  prev.onContextMenu === next.onContextMenu &&
  sameRow(prev.r, next.r)
))

export default Row
