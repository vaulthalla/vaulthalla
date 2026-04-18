'use client'

import React, { memo, useRef, useState } from 'react'
import { Table, TableHeader, TableBody, TableRow, TableHead } from '@/components/table'
import { ScrollArea } from '@/components/ScrollArea'
import { CardContent } from '@/components/card/CardContent'
import { Card } from '@/components/card/Card'
import type { File as FileModel } from '@/models/file'
import { useFSStore } from '@/stores/fsStore'
import { FilePreviewModal } from './FilePreviewModal'
import { ContextMenu } from '@/components/fs/ContextMenu'
import Row from '@/components/fs/Row'
import type { FilesystemRow } from '@/components/fs/types'

type FilesystemClientProps = { rows: FilesystemRow[] }

export const FilesystemClient: React.FC<FilesystemClientProps> = memo(({ rows }) => {
  const [selectedFile, setSelectedFile] = useState<FileModel | null>(null)
  const [contextMenu, setContextMenu] = useState<{ mouseX: number; mouseY: number; row: FilesystemRow } | null>(null)
  const tableRef = useRef<HTMLDivElement>(null)

  const { setCopiedItem, copiedItem, pasteCopiedItem, setPath, fetchFiles, delete: deleteItem } = useFSStore()

  const handleOpenFile = React.useCallback((f: FileModel) => setSelectedFile(f), [])

  const handleRowContextMenu = React.useCallback((e: React.MouseEvent, row: FilesystemRow) => {
    setContextMenu({ mouseX: e.clientX, mouseY: e.clientY, row })
  }, [])

  const handleDelete = React.useCallback((entryName: string) => {
    deleteItem(entryName)
      .then(() => fetchFiles())
      .catch(err => console.error('Error deleting file:', err))
  }, [deleteItem, fetchFiles])

  const onNavigate = React.useCallback((path: string) => {
    setPath(path)
  }, [setPath])

  const handleTableClick = React.useCallback(
    async (e: React.MouseEvent) => {
      if (!copiedItem) return

      const target = e.target as HTMLElement
      const isRow = target.closest('tr')

      if (!isRow) await pasteCopiedItem()
    },
    [copiedItem, pasteCopiedItem],
  )

  const Header = () => (
    <TableHeader>
      <TableRow className="bg-gray-800/50">
        <TableHead className="w-1/2 pl-6 text-gray-300">Name</TableHead>
        <TableHead className="w-1/10 text-gray-300">Size</TableHead>
        <TableHead className="w-1/4 text-gray-300">Last Modified</TableHead>
      </TableRow>
    </TableHeader>
  )

  const Body = () => (
    <TableBody>
      {rows.map(r => (
        <Row
          key={r.key}
          r={r}
          onNavigate={onNavigate}
          onOpenFile={handleOpenFile}
          onContextMenu={handleRowContextMenu}
        />
      ))}
    </TableBody>
  )

  return (
    <>
      <Card
        className="min-h-[90vh] rounded-xl border border-gray-700 bg-gray-900/90 shadow-lg"
        onClick={() => setContextMenu(null)}>
        <CardContent className="p-0">
          <ScrollArea className="h-full">
            <div ref={tableRef} onClick={handleTableClick}>
              <Table>
                <Header />
                <Body />
              </Table>
            </div>
          </ScrollArea>
        </CardContent>
      </Card>

      <FilePreviewModal file={selectedFile} onClose={() => setSelectedFile(null)} />

      {contextMenu && (
        <ContextMenu
          data={contextMenu.row}
          position={{ x: contextMenu.mouseX, y: contextMenu.mouseY }}
          onClose={() => setContextMenu(null)}
          onDelete={row => handleDelete(row.name)}
          onCopy={row => setCopiedItem(row)}
        />
      )}
    </>
  )
})

FilesystemClient.displayName = 'FilesystemClient'
