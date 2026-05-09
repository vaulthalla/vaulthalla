import type { FilesystemRow } from '@/components/fs/types'

export type PreviewMode = 'authenticated' | 'share'

export type PreviewBatchStatus = 'ready' | 'queued' | 'missing' | 'unsupported' | 'error'

export interface PreviewBatchCandidate {
  key: string
  signature: string
  path: string
  vaultId: number | null
  directUrl: string
}

const SIGNATURE_SEPARATOR = '\x1f'

export const toPreviewBatchCandidate = (
  row: FilesystemRow,
  previewMode: PreviewMode,
): PreviewBatchCandidate | null => {
  if (row.entryType !== 'file' || !row.previewUrl) return null

  const path = row.path || row.name
  if (!path) return null
  if (previewMode === 'authenticated' && !row.vault_id) return null

  return {
    key: row.key,
    signature: [
      previewMode,
      row.key,
      row.vault_id || '',
      path,
      row.previewUrl,
      row.updated_at || '',
      row.size_bytes || '',
      row.mime_type || '',
    ].join(SIGNATURE_SEPARATOR),
    path,
    vaultId: row.vault_id || null,
    directUrl: row.previewUrl,
  }
}
