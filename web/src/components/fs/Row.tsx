import Thumb from '@/components/fs/Thumb'
import ArrowRight from '@/fa-duotone/arrow-right.svg'
import Folder from '@/fa-duotone/folder.svg'
import FileIcon from '@/fa-duotone/file.svg'
import React from 'react'
import * as motion from 'motion/react-client'
import type { RowProps } from '@/components/fs/types'

const Row = React.memo(function Row({ r, onNavigate, onOpenFile, onContextMenu }: RowProps) {
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
    <motion.tr
      initial={{ opacity: 0, x: -6 }}
      animate={{ opacity: 1, x: 0 }}
      exit={{ opacity: 0, x: 6 }}
      whileHover={{ y: -1 }}
      transition={{ duration: 0.12 }}
      className="group border-b border-gray-800/60 hover:bg-gray-800/70"
      onContextMenu={handleCtx}
      onClick={click}>
      <td className="px-4 py-2.5 align-middle text-white">
        <div className="flex min-w-0 items-center gap-2">
          <div className="shrink-0">
            {r.entryType === 'file' && r.previewUrl ?
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
    </motion.tr>
  )
})

export default Row
