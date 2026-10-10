import { formatBytes } from '@/lib/format'
import { REMOTE_PREVIEW_LIMIT, sideUrl, type Side } from '@/features/syncConflicts/model'

export type SideBytes = { ok: true; blob: Blob } | { ok: false; message: string }

const reasonOf = async (response: Response): Promise<string | null> => {
  try {
    const body = (await response.json()) as { reason?: unknown; error?: unknown }
    if (typeof body.reason === 'string' && body.reason) return body.reason
    if (typeof body.error === 'string' && body.error) return body.error
  } catch {
    // not JSON
  }
  return null
}

// One copy's bytes from `GET /download/conflict`. The remote side is fetched from the bucket on demand (metered,
// price-preflighted, capped at 32 MiB by the daemon), so callers fetch it once per opened preview.
export const fetchSide = async (conflictId: number, side: Side, signal?: AbortSignal): Promise<SideBytes> => {
  let response: Response
  try {
    response = await fetch(sideUrl(conflictId, side), { credentials: 'same-origin', cache: 'no-store', signal })
  } catch (error) {
    if (signal?.aborted) throw error
    return { ok: false, message: 'The server could not be reached.' }
  }
  if (response.ok) return { ok: true, blob: await response.blob() }
  const reason = await reasonOf(response)
  switch (response.status) {
    case 401:
      return { ok: false, message: 'Your session expired. Reload the page to sign in again.' }
    case 403:
      return { ok: false, message: 'You don’t have permission to read this file.' }
    case 404:
      return { ok: false, message: 'This file or conflict no longer exists.' }
    case 409:
      return { ok: false, message: 'This conflict was already resolved or closed.' }
    case 413:
      return { ok: false, message: `Too large to preview from the bucket (limit ${formatBytes(REMOTE_PREVIEW_LIMIT)}).` }
    case 503:
      return {
        ok: false,
        message: `The ${side === 'remote' ? 'bucket' : 'local'} copy is unavailable right now${reason && reason !== 'content_unavailable' ? `: ${reason}` : '.'}`,
      }
    default:
      return { ok: false, message: `Loading failed (HTTP ${response.status}${reason ? `: ${reason}` : ''}).` }
  }
}

// Strict UTF-8 without NUL bytes, or null (binary / another encoding).
export const decodeText = async (blob: Blob): Promise<string | null> => {
  try {
    const text = new TextDecoder('utf-8', { fatal: true }).decode(await blob.arrayBuffer())
    return text.includes('\u0000') ? null : text
  } catch {
    return null
  }
}
