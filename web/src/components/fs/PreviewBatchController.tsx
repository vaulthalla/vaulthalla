'use client'

import React from 'react'
import type { PreviewBatchCandidate, PreviewBatchStatus, PreviewMode } from '@/components/fs/previewBatch'
import { usePreviewCacheStore } from '@/components/fs/previewCacheStore'

interface PreviewBatchControllerProps {
  candidates: PreviewBatchCandidate[]
  previewMode: PreviewMode
  signature: string
}

interface PreviewBatchResponse {
  size?: number
  items: Array<{
    key?: string
    status: PreviewBatchStatus
    url?: string
    size?: number
  }>
}

const PREVIEW_BATCH_MAX_QUEUE_POLLS = 24
const previewBatchPollDelay = (attempt: number) => Math.min(5000, 500 + attempt * 500)

export const PreviewBatchController = ({ candidates, previewMode, signature }: PreviewBatchControllerProps) => {
  const setPreviewResults = usePreviewCacheStore(state => state.setPreviewResults)
  const candidatesRef = React.useRef(candidates)
  candidatesRef.current = candidates

  React.useEffect(() => {
    const requestCandidates = candidatesRef.current
    if (!signature || !requestCandidates.length) return

    let cancelled = false
    let timer: ReturnType<typeof setTimeout> | null = null
    const candidatesByKey = new Map(requestCandidates.map(candidate => [candidate.key, candidate]))

    const requestBatch = async (attempt: number) => {
      try {
        const response = await fetch(`/preview/batch${previewMode === 'share' ? '?share=1' : ''}`, {
          method: 'POST',
          credentials: 'same-origin',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            size: 128,
            ...(previewMode === 'authenticated' ? { vault_id: requestCandidates[0]?.vaultId } : {}),
            items: requestCandidates.map(candidate => ({
              key: candidate.key,
              ...(previewMode === 'authenticated' ? { vault_id: candidate.vaultId } : {}),
              path: candidate.path,
            })),
          }),
        })
        if (!response.ok) throw new Error(await response.text())

        const data = await response.json() as PreviewBatchResponse
        if (cancelled) return

        let queued = false
        const updates = data.items.flatMap(item => {
          if (!item.key) return []
          const candidate = candidatesByKey.get(item.key)
          if (!candidate) return []
          if (item.status === 'queued') queued = true

          return [{
            key: candidate.key,
            signature: candidate.signature,
            status: item.status,
            url: item.status === 'ready' && item.url ? item.url : null,
          }]
        })

        setPreviewResults(updates)

        if (queued && attempt < PREVIEW_BATCH_MAX_QUEUE_POLLS)
          timer = setTimeout(() => requestBatch(attempt + 1), previewBatchPollDelay(attempt))
      } catch (error) {
        if (cancelled) return
        console.warn('[PreviewBatchController] Preview batch unavailable, keeping direct preview URLs:', error)
      }
    }

    requestBatch(0)

    return () => {
      cancelled = true
      if (timer) clearTimeout(timer)
    }
  }, [previewMode, setPreviewResults, signature])

  return null
}
