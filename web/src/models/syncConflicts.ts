import type { IPreviewPlan } from '@/models/file'

// Sync conflicts recorded under the `ask` remote conflict policy (#187). Core only returns conflicts in vaults where
// the caller holds vault.sync.action.resolve_conflicts; resolving also needs filesystem Overwrite on the file.

export type SyncConflictResolution = 'keep_local' | 'keep_remote'

export interface SyncConflictSide {
  size_bytes: number
  mime_type: string | null
  content_hash: string | null
  modified_at: string | null // ISO 8601 UTC
  etag: string | null // remote side only
  encrypted: boolean | null // remote: Vaulthalla-encrypted upstream; local: null
}

export interface SyncConflictReason {
  code: string
  message: string
}

export interface SyncConflict {
  id: number
  vault_id: number
  vault_name: string
  file_id: number
  path: string // vault path
  name: string
  type: 'mismatch' | 'encryption' | 'both'
  reasons: SyncConflictReason[]
  created_at: string
  updated_at: string
  local: SyncConflictSide
  remote: SyncConflictSide
  // The caller holds filesystem Overwrite on the file; without it a resolve is denied.
  can_overwrite: boolean
  preview: IPreviewPlan
}

export interface SyncConflictVaultCount {
  vault_id: number
  vault_name: string
  count: number
}

export interface SyncConflictSummary {
  total: number
  vaults: SyncConflictVaultCount[]
}

export type SyncConflictResultStatus = 'resolved' | 'denied' | 'not_found' | 'conflict' | 'invalid' | 'unavailable' | 'error'

export interface SyncConflictResolveResult {
  conflict_id: number
  ok: boolean
  status: SyncConflictResultStatus
  message: string | null
}

export interface SyncConflictResolveResponse {
  resolution: SyncConflictResolution
  resolved: number
  failed: number
  results: SyncConflictResolveResult[]
}
