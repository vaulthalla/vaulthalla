'use client'

import React, { memo, useState } from 'react'
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
import { ShareManagementModal } from '@/components/share/ShareManagementModal'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { canRequestSharePreview, hasEffectiveShareOperation } from '@/util/shareOperations'
import { PreviewBatchController } from '@/components/fs/PreviewBatchController'
import { toPreviewBatchCandidate } from '@/components/fs/previewBatch'
import { isPreviewableMime } from '@/util/previewMime'

type FilesystemClientProps = { rows: FilesystemRow[]; previewMode: 'authenticated' | 'share' }

const SharePreviewWarning = () => {
  const mode = useFSStore(state => state.mode)
  const previewError = useFSStore(state => state.previewError)
  const downloading = useFSStore(state => state.downloading)

  if (mode !== 'share' || !previewError || downloading) return null

  return (
    <div className="mt-3 rounded border border-amber-500/40 bg-amber-950/30 p-3 text-sm text-amber-100">
      Preview unavailable. Falling back to download when permitted.
    </div>
  )
}

const SelectedFilePreviewModal = ({
  file,
  onClose,
}: {
  file: FileModel | null
  onClose: () => void
}) => {
  const sharePreview = useFSStore(state => state.sharePreview)
  const clearSharePreview = useFSStore(state => state.clearSharePreview)

  return (
    <FilePreviewModal
      file={file}
      sharePreview={sharePreview}
      onClose={() => {
        onClose()
        clearSharePreview()
      }}
    />
  )
}

const ShareManagementHost = ({
  target,
  onClose,
}: {
  target: FilesystemRow | null
  onClose: () => void
}) => {
  const currVault = useFSStore(state => state.currVault)
  if (!target || !currVault) return null

  return <ShareManagementModal target={target} vault={currVault} onClose={onClose} />
}

export const FilesystemClient: React.FC<FilesystemClientProps> = memo(({ rows, previewMode }) => {
  const [selectedFile, setSelectedFile] = useState<FileModel | null>(null)
  const [shareTarget, setShareTarget] = useState<FilesystemRow | null>(null)
  const [contextMenu, setContextMenu] = useState<{ mouseX: number; mouseY: number; row: FilesystemRow } | null>(null)

  const setCopiedItem = useFSStore(state => state.setCopiedItem)
  const copiedItem = useFSStore(state => state.copiedItem)
  const pasteCopiedItem = useFSStore(state => state.pasteCopiedItem)
  const setPath = useFSStore(state => state.setPath)
  const fetchFiles = useFSStore(state => state.fetchFiles)
  const deleteItem = useFSStore(state => state.delete)
  const mode = useFSStore(state => state.mode)
  const downloadFile = useFSStore(state => state.downloadFile)
  const clearSharePreview = useFSStore(state => state.clearSharePreview)
  const share = useVaultShareStore(state => state.share)
  const canSharePreview = canRequestSharePreview(share)
  const canShareDownload = hasEffectiveShareOperation(share, 'download')
  const canManageShares = mode === 'authenticated'

  const previewCandidates = React.useMemo(() => rows.flatMap(row => {
    const candidate = toPreviewBatchCandidate(row, previewMode)
    return candidate ? [candidate] : []
  }), [previewMode, rows])

  const previewCandidateSignature = React.useMemo(
    () => previewCandidates.map(candidate => candidate.signature).join('\n'),
    [previewCandidates],
  )

  const sharePath = React.useCallback((row: { path?: string; name: string }) => row.path || row.name, [])
  const isPreviewable = React.useCallback((file: FileModel) => isPreviewableMime(file.mime_type), [])

  const handleOpenFile = React.useCallback(
    (f: FileModel) => {
      if (mode === 'share') {
        const { downloading, previewing } = useFSStore.getState()
        if (downloading || previewing) return
        const path = sharePath(f)
        clearSharePreview()
        if (canSharePreview && isPreviewable(f)) {
          setSelectedFile(f)
          return
        }
        if (canShareDownload) {
          downloadFile(path).catch(err => console.error('Error downloading shared file:', err))
          return
        }
        window.alert('No available action for this file in the current share.')
        return
      }

      setSelectedFile(f)
    },
    [canShareDownload, canSharePreview, clearSharePreview, downloadFile, isPreviewable, mode, sharePath],
  )

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

  const handleDownload = React.useCallback(
    (row: FilesystemRow) => {
      const path = mode === 'share' ? sharePath(row) : (row.path ?? row.name)
      downloadFile(path).catch(err => console.error('Error downloading file:', err))
    },
    [downloadFile, mode, sharePath],
  )

  const handleSharePreview = React.useCallback((row: FilesystemRow) => {
    if (row.entryType === 'file') {
      clearSharePreview()
      setSelectedFile(row)
    }
  }, [clearSharePreview])

  const handleShareOpen = React.useCallback((row: FilesystemRow) => {
    if (row.entryType === 'directory') setPath(row.path ?? row.name)
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
  const closeSelectedFile = React.useCallback(() => setSelectedFile(null), [])
  const closeShareTarget = React.useCallback(() => setShareTarget(null), [])

  return (
    <>
      <PreviewBatchController candidates={previewCandidates} previewMode={previewMode} signature={previewCandidateSignature} />

      <Card
        className="min-h-[90vh] rounded-xl border border-gray-700 bg-gray-900/90 shadow-lg"
        onClick={() => setContextMenu(null)}>
        <CardContent className="p-0">
          <ScrollArea className="h-full">
            <div onClick={handleTableClick}>
              <Table>
                <TableHeader>
                  <TableRow className="bg-gray-800/50">
                    <TableHead className="w-1/2 pl-6 text-gray-300">Name</TableHead>
                    <TableHead className="w-1/10 text-gray-300">Size</TableHead>
                    <TableHead className="w-1/4 text-gray-300">Last Modified</TableHead>
                  </TableRow>
                </TableHeader>
                <TableBody>
                  {rows.map(r => (
                    <Row
                      key={r.key}
                      r={r}
                      previewMode={previewMode}
                      onNavigate={onNavigate}
                      onOpenFile={handleOpenFile}
                      onContextMenu={handleRowContextMenu}
                    />
                  ))}
                </TableBody>
              </Table>
            </div>
          </ScrollArea>
        </CardContent>
      </Card>

      <SharePreviewWarning />
      <SelectedFilePreviewModal file={selectedFile} onClose={closeSelectedFile} />

      {contextMenu && (
        <ContextMenu
          data={contextMenu.row}
          position={{ x: contextMenu.mouseX, y: contextMenu.mouseY }}
          onClose={() => setContextMenu(null)}
          onDelete={row => handleDelete(row.name)}
          onCopy={row => setCopiedItem(row)}
          onShare={canManageShares ? row => setShareTarget(row) : undefined}
          onOpen={handleShareOpen}
          onPreview={handleSharePreview}
          onDownload={handleDownload}
        />
      )}

      {canManageShares && <ShareManagementHost target={shareTarget} onClose={closeShareTarget} />}
    </>
  )
})

FilesystemClient.displayName = 'FilesystemClient'
