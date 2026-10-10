'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { cn } from '@/util/cn'
import { Spinner } from '@/components/ui/Spinner'
import { formatInt } from '@/lib/format'
import { collapse, diffRows, type DiffRow } from '@/features/syncConflicts/lineDiff'
import { decodeText, fetchSide } from '@/features/syncConflicts/fetchSide'

type Load = { status: 'loading' } | { status: 'error'; message: string } | { status: 'ready'; local: string; remote: string }

const MAX_ITEMS = 4000

const Cell = ({ line, tone }: { line: DiffRow['left']; tone: 'local' | 'remote' | 'same' }) => (
  <div
    className={cn(
      'grid min-w-0 grid-cols-[3rem_1fr] font-mono text-xs leading-5',
      tone === 'local' && (line ? 'bg-warn-soft' : 'bg-surface-2'),
      tone === 'remote' && (line ? 'bg-info-soft' : 'bg-surface-2'),
    )}>
    <span className="tabular pr-2 text-right text-fg-faint select-none">{line?.no ?? ''}</span>
    <span className="min-w-0 pr-2 break-all whitespace-pre-wrap text-fg">{line?.text ?? ''}</span>
  </div>
)

// Side-by-side line diff of the two copies (local left, remote right). Loaded lazily by the conflict sheet; fetches
// both sides itself once the remote fetch is allowed.
export const TextDiff = ({ conflictId, remoteAllowed }: { conflictId: number; remoteAllowed: boolean }) => {
  const [load, setLoad] = useState<Load>({ status: 'loading' })

  useEffect(() => {
    if (!remoteAllowed) return
    const controller = new AbortController()
    setLoad({ status: 'loading' })
    void (async () => {
      try {
        const [local, remote] = await Promise.all([
          fetchSide(conflictId, 'local', controller.signal),
          fetchSide(conflictId, 'remote', controller.signal),
        ])
        if (!local.ok) return setLoad({ status: 'error', message: `Local copy: ${local.message}` })
        if (!remote.ok) return setLoad({ status: 'error', message: `Bucket copy: ${remote.message}` })
        const [l, r] = await Promise.all([decodeText(local.blob), decodeText(remote.blob)])
        if (l === null || r === null) return setLoad({ status: 'error', message: 'One of the copies is not UTF-8 text, so there is no line diff.' })
        setLoad({ status: 'ready', local: l, remote: r })
      } catch {
        // aborted: the sheet closed
      }
    })()
    return () => controller.abort()
  }, [conflictId, remoteAllowed])

  const view = useMemo(() => {
    if (load.status !== 'ready') return null
    const rows = diffRows(load.local, load.remote)
    if (!rows) return { rows: null, items: [], changes: 0 }
    return { rows, items: collapse(rows), changes: rows.filter(r => r.kind === 'change').length }
  }, [load])

  if (!remoteAllowed) return null
  if (load.status === 'loading')
    return (
      <div className="flex justify-center py-8">
        <Spinner className="size-5" label="Loading both copies" />
      </div>
    )
  if (load.status === 'error') return <p className="text-sm text-fg-subtle">{load.message}</p>
  if (!view) return null

  if (!view.rows)
    return (
      <div className="space-y-2">
        <p className="text-sm text-fg-subtle">The copies differ on too many lines for an aligned diff; here they are as they are.</p>
        <div className="grid gap-2 md:grid-cols-2">
          {[load.local, load.remote].map((text, index) => (
            <pre key={index} className="max-h-[50vh] overflow-auto rounded-control border border-line bg-surface-1 p-2 font-mono text-xs break-all whitespace-pre-wrap text-fg">
              {text}
            </pre>
          ))}
        </div>
      </div>
    )

  if (view.changes === 0)
    return <p className="text-sm text-fg-subtle">The text is identical line by line (the copies differ only in line endings or metadata).</p>

  const items = view.items.slice(0, MAX_ITEMS)
  return (
    <div className="space-y-2" data-testid="conflict-text-diff">
      <p className="text-xs text-fg-subtle">
        {formatInt(view.changes)} changed line{view.changes === 1 ? '' : 's'}. Left: local (vault), right: remote (bucket).
      </p>
      <div className="max-h-[55vh] overflow-auto rounded-control border border-line">
        {items.map((item, index) =>
          item.type === 'gap' ? (
            <div key={index} className="border-y border-line bg-surface-2 px-3 py-0.5 text-center text-[11px] text-fg-subtle">
              {formatInt(item.count)} unchanged line{item.count === 1 ? '' : 's'}
            </div>
          ) : (
            <div key={index} className="grid grid-cols-2 divide-x divide-line">
              <Cell line={item.row.left} tone={item.row.kind === 'change' ? 'local' : 'same'} />
              <Cell line={item.row.right} tone={item.row.kind === 'change' ? 'remote' : 'same'} />
            </div>
          ),
        )}
        {view.items.length > MAX_ITEMS ? (
          <div className="px-3 py-1 text-center text-[11px] text-fg-subtle">Diff truncated after {formatInt(MAX_ITEMS)} rows.</div>
        ) : null}
      </div>
    </div>
  )
}
