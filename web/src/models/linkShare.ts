export type ShareTargetType = 'file' | 'directory'
export type ShareLinkType = 'download' | 'upload' | 'access'
export type ShareAccessMode = 'public' | 'email_validated'
export type ShareOperation = 'metadata' | 'list' | 'preview' | 'download' | 'upload' | 'mkdir' | 'overwrite'
export type ShareDuplicatePolicy = 'reject' | 'rename' | 'overwrite'

export type ShareStatus = 'idle' | 'opening' | 'email_required' | 'ready' | 'expired' | 'revoked' | 'error'

export interface ShareConstraints {
  max_downloads?: number | null
  max_upload_files?: number | null
  max_upload_size_bytes?: number | null
  max_upload_total_bytes?: number | null
  duplicate_policy?: ShareDuplicatePolicy
  allowed_mime_types?: string[]
  blocked_mime_types?: string[]
  allowed_extensions?: string[]
  blocked_extensions?: string[]
}

export interface ShareLink {
  id: string
  created_by?: number
  updated_by?: number | null
  revoked_by?: number | null
  vault_id: number
  root_entry_id: number
  root_path: string
  target_type: ShareTargetType
  link_type: ShareLinkType
  access_mode: ShareAccessMode
  allowed_ops: number | ShareOperation[]
  name?: string | null
  public_label?: string | null
  description?: string | null
  expires_at?: string | null
  revoked_at?: string | null
  disabled_at?: string | null
  created_at?: string
  updated_at?: string
  last_accessed_at?: string | null
  access_count?: number
  download_count?: number
  upload_count?: number
  max_downloads?: number | null
  max_upload_files?: number | null
  max_upload_size_bytes?: number | null
  max_upload_total_bytes?: number | null
  duplicate_policy?: ShareDuplicatePolicy
  allowed_mime_types?: string[]
  blocked_mime_types?: string[]
  allowed_extensions?: string[]
  blocked_extensions?: string[]
  metadata?: Record<string, unknown>
}

export interface PublicShare {
  id: string
  root_path?: string
  target_type: ShareTargetType
  link_type: ShareLinkType
  access_mode: ShareAccessMode
  allowed_ops: number | ShareOperation[]
  public_label?: string | null
  expires_at?: string | null
  metadata?: Record<string, unknown>
  constraints?: ShareConstraints
}

export interface ShareLinkCreatePayload {
  vault_id: number
  root_entry_id: number
  root_path: string
  target_type: ShareTargetType
  link_type: ShareLinkType
  access_mode: ShareAccessMode
  allowed_ops: number | ShareOperation[]
  name?: string | null
  public_label?: string | null
  description?: string | null
  expires_at?: string | number | null
  max_downloads?: number | null
  max_upload_files?: number | null
  max_upload_size_bytes?: number | null
  max_upload_total_bytes?: number | null
  duplicate_policy?: ShareDuplicatePolicy
  allowed_mime_types?: string[]
  blocked_mime_types?: string[]
  allowed_extensions?: string[]
  blocked_extensions?: string[]
  metadata?: Record<string, unknown>
}

export type ShareLinkUpdatePayload = Partial<Omit<ShareLinkCreatePayload, 'vault_id' | 'root_entry_id' | 'root_path' | 'target_type'>> & {
  id: string
}

export interface ShareLinkTokenResponse {
  share: ShareLink
  public_token: string
  public_url_path: string
}

export interface ShareLinkResponse {
  share: ShareLink
}

export interface ShareLinkListResponse {
  shares: ShareLink[]
}

export interface ShareSessionOpenResponse {
  status: 'ready' | 'email_required'
  share: PublicShare
  session_id: string
  session_token: string
}

export interface ShareEmailChallengeStartResponse {
  status: 'challenge_created'
  share: PublicShare
  challenge_id: string
  session_id: string
  session_token?: string
}

export interface ShareEmailChallengeConfirmResponse {
  status: 'ready' | 'email_required'
  verified: boolean
  session_id: string
}

export interface ShareEntry {
  id: number
  name: string
  path: string
  type: ShareTargetType
  size_bytes: number
  created_at?: string
  updated_at?: string
  file_count?: number
  subdirectory_count?: number
  mime_type?: string | null
}

export interface ShareMetadataResponse {
  entry: ShareEntry
}

export interface ShareListResponse {
  path: string
  entries: ShareEntry[]
}

export interface ShareDownloadStartResponse {
  transfer_id: string
  filename: string
  path: string
  size_bytes: number
  mime_type?: string | null
  chunk_size: number
}

export interface ShareDownloadChunkResponse {
  transfer_id: string
  offset: number
  bytes: number
  data_base64: string
  next_offset: number
  complete: boolean
}

export interface ShareDownloadCancelResponse {
  cancelled: boolean
  transfer_id: string
}

export interface SharePreviewResponse {
  path: string
  filename: string
  source_mime_type: string
  mime_type: string
  size_bytes: number
  data_base64: string
}

export interface ShareUploadStartResponse {
  upload_id: string
  transfer_id: string
  path: string
  filename: string
  size_bytes: number
  chunk_size: number
  duplicate_policy: ShareDuplicatePolicy
}

export interface ShareUploadFinishResponse {
  upload_id: string
  complete: boolean
  entry: ShareEntry
}

export interface ShareUploadCancelResponse {
  cancelled: boolean
  upload_id: string
}
