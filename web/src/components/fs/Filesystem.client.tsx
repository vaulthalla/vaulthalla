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
import { ShareManagementModal } from '@/components/share/ShareManagementModal'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { canRequestSharePreview, hasEffectiveShareOperation } from '@/util/shareOperations'

type FilesystemClientProps = { rows: FilesystemRow[]; previewMode: 'authenticated' | 'share' }

interface PreviewBatchResponse {
  size?: number
  items: Array<{
    key?: string
    status: 'ready' | 'queued' | 'missing' | 'unsupported' | 'error'
    url?: string
    size?: number
  }>
}

export const FilesystemClient: React.FC<FilesystemClientProps> = memo(({ rows, previewMode }) => {
  const [selectedFile, setSelectedFile] = useState<FileModel | null>(null)
  const [shareTarget, setShareTarget] = useState<FilesystemRow | null>(null)
  const [contextMenu, setContextMenu] = useState<{ mouseX: number; mouseY: number; row: FilesystemRow } | null>(null)
  const [batchedPreviewUrls, setBatchedPreviewUrls] = useState<Record<string, string | null | undefined>>({})
  const tableRef = useRef<HTMLDivElement>(null)

  const setCopiedItem = useFSStore(state => state.setCopiedItem)
  const copiedItem = useFSStore(state => state.copiedItem)
  const pasteCopiedItem = useFSStore(state => state.pasteCopiedItem)
  const setPath = useFSStore(state => state.setPath)
  const fetchFiles = useFSStore(state => state.fetchFiles)
  const deleteItem = useFSStore(state => state.delete)
  const currVault = useFSStore(state => state.currVault)
  const mode = useFSStore(state => state.mode)
  const downloadFile = useFSStore(state => state.downloadFile)
  const downloading = useFSStore(state => state.downloading)
  const previewing = useFSStore(state => state.previewing)
  const previewError = useFSStore(state => state.previewError)
  const sharePreview = useFSStore(state => state.sharePreview)
  const clearSharePreview = useFSStore(state => state.clearSharePreview)
  const share = useVaultShareStore(state => state.share)
  const canSharePreview = canRequestSharePreview(share)
  const canShareDownload = hasEffectiveShareOperation(share, 'download')
  const canManageShares = mode === 'authenticated'

  const previewCandidates = React.useMemo(() => (
    rows.filter(row => row.entryType === 'file' && row.previewUrl && row.path && (previewMode === 'share' || row.vault_id))
  ), [previewMode, rows])

  const previewCandidateSignature = React.useMemo(
    () => previewCandidates.map(row => `${row.key}:${row.vault_id}:${row.path}`).join('\n'),
    [previewCandidates],
  )

  React.useEffect(() => {
    if (!previewCandidates.length) {
      setBatchedPreviewUrls({})
      return
    }

    let cancelled = false
    let timer: ReturnType<typeof setTimeout> | null = null

    const requestBatch = async (attempt: number) => {
      try {
        const response = await fetch(`/preview/batch${previewMode === 'share' ? '?share=1' : ''}`, {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            size: 128,
            ...(previewMode === 'authenticated' ? { vault_id: previewCandidates[0]?.vault_id } : {}),
            items: previewCandidates.map(row => ({
              key: row.key,
              ...(previewMode === 'authenticated' ? { vault_id: row.vault_id } : {}),
              path: row.path || row.name,
            })),
          }),
        })
        if (!response.ok) throw new Error(await response.text())
        const data = await response.json() as PreviewBatchResponse
        if (cancelled) return

        const next: Record<string, string | null> = Object.fromEntries(
          previewCandidates.map(row => [row.key, row.previewUrl]),
        )
        let queued = false
        for (const item of data.items) {
          if (!item.key) continue
          if (item.url) next[item.key] = item.url
          if (item.status === 'queued') queued = true
        }
        setBatchedPreviewUrls(next)

        if (queued && attempt < 6)
          timer = setTimeout(() => requestBatch(attempt + 1), Math.min(3000, 500 + attempt * 400))
      } catch (error) {
        if (cancelled) return
        console.warn('[FilesystemClient] Preview batch unavailable, falling back to direct preview URLs:', error)
        setBatchedPreviewUrls(Object.fromEntries(previewCandidates.map(row => [row.key, row.previewUrl])))
      }
    }

    requestBatch(0)

    return () => {
      cancelled = true
      if (timer) clearTimeout(timer)
    }
  }, [previewMode, previewCandidateSignature, previewCandidates])

  const displayRows = React.useMemo(() => rows.map(row => {
    if (row.entryType !== 'file' || !row.previewUrl) return row
    const batchedUrl = batchedPreviewUrls[row.key]
    return {
      ...row,
      previewUrl: batchedUrl === undefined ? row.previewUrl : batchedUrl,
    }
  }), [batchedPreviewUrls, rows])

  const sharePath = React.useCallback((row: { path?: string; name: string }) => row.path || row.name, [])
  const isPreviewable = React.useCallback((file: FileModel) => Boolean(file.mime_type?.startsWith('image/') || file.mime_type === 'application/pdf'), [])

  const handleOpenFile = React.useCallback(
    (f: FileModel) => {
      if (mode === 'share') {
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
    [canShareDownload, canSharePreview, clearSharePreview, downloadFile, downloading, isPreviewable, mode, previewing, sharePath],
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
      {displayRows.map(r => (
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

      {mode === 'share' && previewError && !downloading && (
        <div className="mt-3 rounded border border-amber-500/40 bg-amber-950/30 p-3 text-sm text-amber-100">
          Preview unavailable. Falling back to download when permitted.
        </div>
      )}

      <FilePreviewModal
        file={selectedFile}
        sharePreview={sharePreview}
        onClose={() => {
          setSelectedFile(null)
          clearSharePreview()
        }}
      />

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

      {shareTarget && currVault && canManageShares && (
        <ShareManagementModal target={shareTarget} vault={currVault} onClose={() => setShareTarget(null)} />
      )}
    </>
  )
})

FilesystemClient.displayName = 'FilesystemClient'
