'use client'

import React, { useEffect, useState } from 'react'
import { Dialog, SheetContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { DefinitionList } from '@/components/ui/Panel'
import { Spinner } from '@/components/ui/Spinner'
import { ChevronLeftIcon, ChevronRightIcon, DownloadIcon, ShareNodesIcon } from '@/components/ui/icons'
import { formatBytes, formatDateTime } from '@/lib/format'
import { isPreviewable, type Entry } from '@/features/files/entries'
import { FileIcon } from '@/features/files/FileIcon'
import type { FsSource } from '@/features/files/source'

export const PreviewSheet = ({
  source,
  entry,
  siblings,
  onOpenChange,
  onSelect,
  onDownload,
  onShare,
}: {
  source: FsSource
  entry: Entry | null
  siblings: Entry[]
  onOpenChange: (open: boolean) => void
  onSelect: (entry: Entry) => void
  onDownload: (entry: Entry) => void
  onShare?: (entry: Entry) => void
}) => {
  const [loaded, setLoaded] = useState(false)
  const files = siblings.filter(e => e.kind === 'file')
  const index = entry ? files.findIndex(e => e.key === entry.key) : -1

  useEffect(() => setLoaded(false), [entry?.key])

  useEffect(() => {
    if (!entry) return
    const onKey = (event: KeyboardEvent) => {
      if (event.key === 'ArrowRight' && index < files.length - 1) onSelect(files[index + 1])
      if (event.key === 'ArrowLeft' && index > 0) onSelect(files[index - 1])
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [entry, files, index, onSelect])

  const previewable = entry && source.caps.preview && isPreviewable(entry)

  return (
    <Dialog open={Boolean(entry)} onOpenChange={onOpenChange}>
      {entry ? (
        <SheetContent
          title={entry.name}
          width="max-w-xl"
          footer={
            <>
              {onShare && source.caps.share ? (
                <Button variant="ghost" onClick={() => onShare(entry)}>
                  <ShareNodesIcon aria-hidden /> Share
                </Button>
              ) : null}
              {source.caps.download ? (
                <Button variant="primary" onClick={() => onDownload(entry)}>
                  <DownloadIcon aria-hidden /> Download
                </Button>
              ) : null}
            </>
          }>
          <div className="relative grid min-h-64 place-items-center overflow-hidden rounded-card border border-line bg-black/40">
            {previewable ? (
              <>
                {!loaded ? <Spinner className="absolute" /> : null}
                {/* eslint-disable-next-line @next/next/no-img-element -- authenticated preview route, not optimizable */}
                <img
                  src={source.previewUrl(entry.path, 1024)}
                  alt={entry.name}
                  onLoad={() => setLoaded(true)}
                  onError={() => setLoaded(true)}
                  className="max-h-[60dvh] w-full object-contain"
                />
              </>
            ) : (
              <div className="flex flex-col items-center gap-3 py-16 text-fg-subtle">
                <FileIcon entry={entry} className="size-10" />
                <span className="text-sm">No preview for this file type</span>
              </div>
            )}
          </div>

          {files.length > 1 ? (
            <div className="mt-3 flex items-center justify-between text-xs text-fg-subtle">
              <Button variant="ghost" size="sm" disabled={index <= 0} onClick={() => onSelect(files[index - 1])}>
                <ChevronLeftIcon aria-hidden /> Previous
              </Button>
              <span className="tabular">
                {index + 1} of {files.length}
              </span>
              <Button variant="ghost" size="sm" disabled={index >= files.length - 1} onClick={() => onSelect(files[index + 1])}>
                Next <ChevronRightIcon aria-hidden />
              </Button>
            </div>
          ) : null}

          <DefinitionList
            className="mt-5"
            items={[
              ['Size', formatBytes(entry.size)],
              ['Type', entry.mime ?? 'Unknown'],
              ['Modified', formatDateTime(entry.modified || null)],
              ['Location', <span key="loc" className="font-mono text-xs break-all">{entry.path}</span>],
            ]}
          />
        </SheetContent>
      ) : null}
    </Dialog>
  )
}
