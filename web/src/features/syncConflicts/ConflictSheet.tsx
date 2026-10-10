'use client'

import React, { useEffect, useState } from 'react'
import dynamic from 'next/dynamic'
import { cn } from '@/util/cn'
import { Dialog, SheetContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { Spinner } from '@/components/ui/Spinner'
import { Tooltip } from '@/components/ui/Tooltip'
import { DASH, formatBytes, formatDateTime } from '@/lib/format'
import {
  NO_OVERWRITE,
  REMOTE_AUTOLOAD_LIMIT,
  REMOTE_PREVIEW_LIMIT,
  RESOLUTION_EFFECT,
  RESOLUTION_LABEL,
  TEXT_DIFF_LIMIT,
  previewCategory,
  shortHash,
  sideUrl,
  type PreviewCategory,
} from '@/features/syncConflicts/model'
import { fetchSide } from '@/features/syncConflicts/fetchSide'
import type { SyncConflict, SyncConflictResolution, SyncConflictSide } from '@/models/syncConflicts'

// The diff (Myers line diff + its view) is its own chunk, loaded only for text conflicts.
const TextDiff = dynamic(() => import('@/features/syncConflicts/TextDiff').then(m => m.TextDiff), {
  ssr: false,
  loading: () => (
    <div className="flex justify-center py-8">
      <Spinner className="size-5" label="Loading diff" />
    </div>
  ),
})

const yesNo = (value: boolean | null) => (value === null ? DASH : value ? 'Yes' : 'No')

const META: { label: string; value: (side: SyncConflictSide) => string; mono?: boolean; title?: (side: SyncConflictSide) => string | undefined }[] = [
  { label: 'Size', value: side => `${formatBytes(side.size_bytes)}` },
  { label: 'Modified', value: side => formatDateTime(side.modified_at) },
  { label: 'Content hash', value: side => shortHash(side.content_hash) ?? DASH, mono: true, title: side => side.content_hash ?? undefined },
  { label: 'Type', value: side => side.mime_type ?? DASH },
  { label: 'ETag', value: side => side.etag ?? DASH, mono: true },
  { label: 'Encrypted in bucket', value: side => yesNo(side.encrypted) },
]

const MetadataTable = ({ conflict }: { conflict: SyncConflict }) => (
  <table className="w-full table-fixed border-collapse text-sm">
    <thead>
      <tr className="border-b border-line text-left text-xs text-fg-subtle">
        <th className="w-36 py-1.5 font-medium" scope="col" />
        <th className="py-1.5 font-medium" scope="col">
          Local (vault)
        </th>
        <th className="py-1.5 font-medium" scope="col">
          Remote (bucket)
        </th>
      </tr>
    </thead>
    <tbody>
      {META.map(row => {
        const local = row.value(conflict.local)
        const remote = row.value(conflict.remote)
        const differs = local !== remote && !(local === DASH || remote === DASH)
        return (
          <tr key={row.label} className="border-b border-line/60 last:border-0">
            <th scope="row" className="py-1.5 pr-3 text-left font-normal text-fg-subtle">
              {row.label}
            </th>
            {[
              [local, conflict.local],
              [remote, conflict.remote],
            ].map(([text, side], index) => (
              <td
                key={index}
                title={row.title?.(side as SyncConflictSide)}
                className={cn('truncate py-1.5 pr-3', row.mono && 'font-mono text-xs', differs ? 'text-warn' : 'text-fg')}>
                {text as string}
              </td>
            ))}
          </tr>
        )
      })}
    </tbody>
  </table>
)

const MediaElement = ({ category, src, label }: { category: PreviewCategory; src: string; label: string }) => {
  if (category === 'image')
    // eslint-disable-next-line @next/next/no-img-element
    return <img src={src} alt={label} className="mx-auto max-h-[50vh] max-w-full rounded-control object-contain" />
  if (category === 'video') return <video src={src} controls preload="metadata" className="max-h-[50vh] w-full rounded-control" aria-label={label} />
  return <audio src={src} controls preload="metadata" className="w-full" aria-label={label} />
}

// The bucket copy as an object URL, fetched once while the sheet is open.
const RemoteMedia = ({ conflictId, category }: { conflictId: number; category: PreviewCategory }) => {
  const [state, setState] = useState<{ url: string } | { error: string } | null>(null)
  useEffect(() => {
    const controller = new AbortController()
    let url: string | null = null
    void fetchSide(conflictId, 'remote', controller.signal)
      .then(result => {
        if (!result.ok) return setState({ error: result.message })
        url = URL.createObjectURL(result.blob)
        setState({ url })
      })
      .catch(() => undefined)
    return () => {
      controller.abort()
      if (url) URL.revokeObjectURL(url)
    }
  }, [conflictId])
  if (!state)
    return (
      <div className="flex justify-center py-8">
        <Spinner className="size-5" label="Loading the bucket copy" />
      </div>
    )
  if ('error' in state) return <p className="text-sm text-fg-subtle">{state.error}</p>
  return <MediaElement category={category} src={state.url} label="Remote copy" />
}

export const ConflictSheet = ({
  conflict,
  busy,
  onClose,
  onResolve,
}: {
  conflict: SyncConflict
  busy: boolean
  onClose: () => void
  onResolve: (resolution: SyncConflictResolution) => Promise<boolean>
}) => {
  const category = previewCategory(conflict)
  const remoteSize = conflict.remote.size_bytes
  const tooLarge = remoteSize > REMOTE_PREVIEW_LIMIT
  // Bucket bytes are metered: small copies load with the sheet, larger ones on an explicit click.
  const [remoteAllowed, setRemoteAllowed] = useState(!tooLarge && remoteSize <= REMOTE_AUTOLOAD_LIMIT)
  const textTooLarge = category === 'text' && (conflict.local.size_bytes > TEXT_DIFF_LIMIT || remoteSize > TEXT_DIFF_LIMIT)

  const loadButton = !remoteAllowed && !tooLarge && (category !== 'none' && !textTooLarge) ? (
    <Button size="sm" variant="secondary" onClick={() => setRemoteAllowed(true)}>
      Load the bucket copy ({formatBytes(remoteSize)}, counts against S3 budgets)
    </Button>
  ) : null

  const resolveButtons = (['keep_local', 'keep_remote'] as const).map(resolution => {
    const button = (
      <Button
        key={resolution}
        variant="secondary"
        disabled={!conflict.can_overwrite || busy}
        loading={busy}
        onClick={() => void onResolve(resolution)}
        title={RESOLUTION_EFFECT[resolution]}>
        {RESOLUTION_LABEL[resolution]}
      </Button>
    )
    return conflict.can_overwrite ? button : <Tooltip key={resolution} content={NO_OVERWRITE}>{button}</Tooltip>
  })

  return (
    <Dialog open onOpenChange={next => !next && onClose()}>
      <SheetContent
        width="max-w-5xl"
        title={conflict.name}
        description={`${conflict.vault_name}: ${conflict.path}`}
        footer={
          <>
            <Button variant="ghost" onClick={onClose}>
              Close
            </Button>
            {resolveButtons}
          </>
        }>
        <div className="space-y-5" data-testid="conflict-sheet">
          {conflict.reasons.length ? (
            <div className="flex flex-wrap gap-1.5">
              {conflict.reasons.map(reason => (
                <Badge key={reason.code} tone="warn" title={reason.code}>
                  {reason.message || reason.code}
                </Badge>
              ))}
            </div>
          ) : null}

          <MetadataTable conflict={conflict} />

          <section aria-label="Preview" className="space-y-3">
            {category === 'none' ? (
              <p className="text-sm text-fg-subtle">There is no side-by-side preview for this file type; compare the details above.</p>
            ) : textTooLarge ? (
              <p className="text-sm text-fg-subtle">One of the copies is larger than {formatBytes(TEXT_DIFF_LIMIT)}, too large for a line diff.</p>
            ) : category === 'text' ? (
              <>
                {loadButton}
                {tooLarge ? <p className="text-sm text-fg-subtle">The bucket copy is too large to preview.</p> : <TextDiff conflictId={conflict.id} remoteAllowed={remoteAllowed} />}
              </>
            ) : (
              <div className="grid gap-4 md:grid-cols-2">
                <div className="min-w-0 space-y-2">
                  <h3 className="text-xs font-medium text-fg-subtle">Local (vault)</h3>
                  <MediaElement category={category} src={sideUrl(conflict.id, 'local')} label="Local copy" />
                </div>
                <div className="min-w-0 space-y-2">
                  <h3 className="text-xs font-medium text-fg-subtle">Remote (bucket)</h3>
                  {tooLarge ? (
                    <p className="text-sm text-fg-subtle">Larger than {formatBytes(REMOTE_PREVIEW_LIMIT)}: too large to preview from the bucket.</p>
                  ) : remoteAllowed ? (
                    <RemoteMedia conflictId={conflict.id} category={category} />
                  ) : (
                    loadButton
                  )}
                </div>
              </div>
            )}
          </section>
        </div>
      </SheetContent>
    </Dialog>
  )
}
