'use client'

import { create } from 'zustand'
import type { PreviewBatchStatus } from '@/components/fs/previewBatch'

export interface PreviewCacheEntry {
  signature: string
  status: PreviewBatchStatus
  url: string | null
}

interface PreviewCacheResult extends PreviewCacheEntry {
  key: string
}

interface PreviewCacheState {
  entries: Record<string, PreviewCacheEntry | undefined>
  setPreviewResults: (results: PreviewCacheResult[]) => void
}

const sameEntry = (a: PreviewCacheEntry | undefined, b: PreviewCacheEntry) => (
  Boolean(a) && a?.signature === b.signature && a.status === b.status && a.url === b.url
)

export const usePreviewCacheStore = create<PreviewCacheState>()(set => ({
  entries: {},
  setPreviewResults(results) {
    if (!results.length) return

    set(state => {
      let nextEntries: Record<string, PreviewCacheEntry | undefined> | null = null

      for (const { key, signature, status, url } of results) {
        const nextEntry = { signature, status, url }
        const prevEntry = state.entries[key]
        if (sameEntry(prevEntry, nextEntry)) continue
        if (!nextEntries) nextEntries = { ...state.entries }
        nextEntries[key] = nextEntry
      }

      return nextEntries ? { entries: nextEntries } : state
    })
  },
}))
