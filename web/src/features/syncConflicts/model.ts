import { api } from '@/lib/session'
import { invalidate } from '@/lib/query'
import type { SyncConflict, SyncConflictResolution, SyncConflictResolveResponse, SyncConflictResultStatus } from '@/models/syncConflicts'

export const CONFLICT_COMMANDS = ['sync.conflicts.list', 'sync.conflicts.summary'] as const

export const RESOLUTION_LABEL: Record<SyncConflictResolution, string> = {
  keep_local: 'Keep local',
  keep_remote: 'Keep remote',
}

export const RESOLUTION_EFFECT: Record<SyncConflictResolution, string> = {
  keep_local: 'The vault’s copy is uploaded over the bucket’s.',
  keep_remote: 'The bucket’s copy is downloaded over the vault’s.',
}

export const STATUS_LABEL: Record<SyncConflictResultStatus, string> = {
  resolved: 'Resolved',
  denied: 'Not allowed',
  not_found: 'Not found',
  conflict: 'Changed since recorded',
  invalid: 'Invalid',
  unavailable: 'Transfer refused',
  error: 'Failed',
}

export const NO_OVERWRITE = 'You need Overwrite permission on this file to resolve it.'

// Core caps one request at 500 ids; larger selections go in consecutive requests and their results are merged.
const BATCH = 500

export const resolveConflicts = async (
  resolution: SyncConflictResolution,
  ids: number[],
): Promise<SyncConflictResolveResponse> => {
  const unique = [...new Set(ids)]
  const merged: SyncConflictResolveResponse = { resolution, resolved: 0, failed: 0, results: [] }
  try {
    for (let i = 0; i < unique.length; i += BATCH) {
      const response = await api.send('sync.conflicts.resolve', { resolution, conflict_ids: unique.slice(i, i + BATCH) })
      merged.resolved += response.resolved ?? 0
      merged.failed += response.failed ?? 0
      merged.results.push(...(response.results ?? []))
    }
  } finally {
    await invalidate(...CONFLICT_COMMANDS)
  }
  return merged
}

// How the preview sheet can show a file: native media/images from bytes, text as a line diff, otherwise metadata only.
export type PreviewCategory = 'image' | 'video' | 'audio' | 'text' | 'none'

export const previewCategory = (conflict: SyncConflict): PreviewCategory => {
  const renderer = conflict.preview?.renderer ?? ''
  const mime = (conflict.local.mime_type ?? conflict.remote.mime_type ?? '').toLowerCase()
  if (renderer === 'image' || renderer === 'svg' || renderer === 'image-native') return 'image'
  if (renderer === 'video') return 'video'
  if (renderer === 'audio') return 'audio'
  if (renderer === 'text' || renderer === 'markdown') return 'text'
  if (renderer && renderer !== 'none') return 'none'
  if (mime.startsWith('image/')) return 'image'
  if (mime.startsWith('video/')) return 'video'
  if (mime.startsWith('audio/')) return 'audio'
  if (mime.startsWith('text/') || mime === 'application/json') return 'text'
  return 'none'
}

export type Side = 'local' | 'remote'

export const sideUrl = (conflictId: number, side: Side) =>
  `/download/conflict?${new URLSearchParams({ conflict_id: String(conflictId), side }).toString()}`

export const TEXT_DIFF_LIMIT = 2 * 1024 * 1024
export const REMOTE_PREVIEW_LIMIT = 32 * 1024 * 1024
// Remote bytes are metered: anything above this waits for an explicit "Load" click instead of loading on open.
export const REMOTE_AUTOLOAD_LIMIT = 2 * 1024 * 1024

export const shortHash = (hash: string | null | undefined) => (hash ? (hash.length > 16 ? `${hash.slice(0, 12)}…` : hash) : null)
