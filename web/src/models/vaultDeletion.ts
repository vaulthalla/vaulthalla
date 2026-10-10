// Safe vault deletion (#162). Mirrors core's vault::model::Deletion JSON (core/src/vault/model/Deletion.cpp) and the
// storage.vault.remove.plan answer (core/src/protocols/ws/handler/vault/Vaults.cpp).

export type VaultDeletionState = 'pending' | 'purging' | 'purged'

export interface VaultDeletion {
  vault_id: number
  vault_name: string
  owner_id: number | null
  owner: string
  type: 'local' | 's3'
  provider: string | null
  bucket: string | null
  encrypt_upstream: boolean | null
  delete_upstream: boolean
  deleted_by: number | null
  deleted_at: string
  // Restorable until the purge starts; purged at (or soon after) this time.
  purge_after: string
  // The sealed key is kept until then, even after the data is purged.
  key_retain_until: string
  key_version: number | null
  key_exported_at: string | null
  key_retained: boolean
  // Encrypted objects stay in the bucket and their key was never exported: warn until it is.
  upstream_key_at_risk: boolean
  // `vh vault keys export …` for this vault (works until key_retain_until).
  export_command: string
  state: VaultDeletionState
  restorable: boolean
  purge_started_at: string | null
  upstream_purged_at: string | null
  purged_at: string | null
  key_purged_at: string | null
  next_attempt_at: string | null
  attempts: number
  // A failed purge attempt (state purging), or why upstream data was kept (state purged).
  last_error: string | null
}

export interface VaultRemovalPlan {
  vault_id: number
  name: string
  type: 'local' | 's3'
  provider: string | null
  bucket: string | null
  encrypted_upstream: boolean
  key_version: number
  key_exported: boolean
  key_exported_at: string | null
  retention_window: string
  retention_window_seconds: number
  key_retention_window: string
  key_retention_window_seconds: number
  // `vh vault keys export …`: the only way to export a key (it never goes through the browser).
  export_command: string
}
