'use client'

import { useEffect, useRef, useSyncExternalStore } from 'react'
import { isPreviewable, type Entry } from '@/features/files/entries'
import type { FsSource } from '@/features/files/source'

// Thumbnails for the rows on screen only. Requests go out in batches of at most 100; only items the server reports
// as still rendering are polled again, with backoff.
type ThumbState = { status: 'ready' | 'queued' | 'missing' | 'unsupported' | 'error'; url: string | null }

const cache = new Map<string, ThumbState>()
const inflight = new Set<string>()
const listeners = new Set<() => void>()
let version = 0
const emit = () => {
  version++
  for (const listener of listeners) listener()
}

const BATCH = 100
const MAX_POLLS = 12

const signature = (source: FsSource, entry: Entry) => `${source.key}\x1f${entry.path}\x1f${entry.modified}\x1f${entry.size ?? ''}`

async function request(source: FsSource, entries: Entry[], attempt: number) {
  const sigs = entries.map(entry => signature(source, entry))
  sigs.forEach(sig => inflight.add(sig))
  try {
    const response = await fetch(`/preview/batch${source.mode === 'share' ? '?share=1' : ''}`, {
      method: 'POST',
      credentials: 'same-origin',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        size: 128,
        ...(source.vaultId ? { vault_id: source.vaultId } : {}),
        items: entries.map((entry, index) => ({ key: sigs[index], path: entry.path, ...(source.vaultId ? { vault_id: source.vaultId } : {}) })),
      }),
    })
    if (!response.ok) throw new Error(String(response.status))
    const data = (await response.json()) as { items?: { key?: string; status: ThumbState['status']; url?: string }[] }
    const requeue: Entry[] = []
    for (const item of data.items ?? []) {
      if (!item.key) continue
      cache.set(item.key, { status: item.status, url: item.status === 'ready' && item.url ? item.url : null })
      if (item.status === 'queued') {
        const entry = entries[sigs.indexOf(item.key)]
        if (entry) requeue.push(entry)
      }
    }
    emit()
    if (requeue.length && attempt < MAX_POLLS) setTimeout(() => void request(source, requeue, attempt + 1), Math.min(5000, 600 * (attempt + 1)))
  } catch {
    // Fall back to the direct preview URL for these rows.
    for (const sig of sigs) if (!cache.has(sig)) cache.set(sig, { status: 'error', url: null })
    emit()
  } finally {
    sigs.forEach(sig => inflight.delete(sig))
  }
}

export const useThumbnails = (source: FsSource, visible: Entry[], enabled: boolean) => {
  const wanted = enabled && source.caps.preview ? visible.filter(isPreviewable) : []
  // A string key keeps the effect stable across renders that show the same rows.
  const wantedKey = wanted.map(entry => signature(source, entry)).join('\n')
  const latest = useRef(wanted)
  latest.current = wanted

  useEffect(() => {
    if (!wantedKey) return
    const timer = setTimeout(() => {
      const missing = latest.current.filter(entry => {
        const sig = signature(source, entry)
        return !cache.has(sig) && !inflight.has(sig)
      })
      for (let i = 0; i < missing.length; i += BATCH) void request(source, missing.slice(i, i + BATCH), 0)
    }, 120)
    return () => clearTimeout(timer)
  }, [source, wantedKey])

  useSyncExternalStore(
    listener => {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
    () => version,
    () => version,
  )

  return (entry: Entry): string | null => {
    if (!source.caps.preview || !isPreviewable(entry)) return null
    const state = cache.get(signature(source, entry))
    if (!state) return null
    if (state.status === 'ready') return state.url
    if (state.status === 'error') return source.previewUrl(entry.path, 128)
    return null
  }
}
