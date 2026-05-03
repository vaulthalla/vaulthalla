import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'
import { VaultStats } from '@/models/stats/vaultStats'
import { APIKey, S3APIKey } from '@/models/apiKey'
import { User } from '@/models/user'
import { AdminRolePayload, VaultRolePayload, Permission } from '@/models/role'
import { Settings } from '@/models/settings'
import { Group } from '@/models/group'
import { File, IFileUpload } from '@/models/file'
import { Directory } from '@/models/directory'
import { CacheStats } from '@/models/stats/cacheStats'
import { AdminRoleDTO, VaultRoleDTO } from '@/models/permission'
import {
  ShareDownloadCancelResponse,
  ShareDownloadChunkResponse,
  ShareDownloadStartResponse,
  ShareEmailChallengeConfirmResponse,
  ShareEmailChallengeStartResponse,
  ShareLinkCreatePayload,
  ShareLinkListResponse,
  ShareLinkResponse,
  ShareLinkTokenResponse,
  ShareLinkUpdatePayload,
  ShareListResponse,
  ShareMetadataResponse,
  SharePreviewResponse,
  ShareSessionOpenResponse,
  ShareUploadCancelResponse,
  ShareUploadFinishResponse,
  ShareUploadStartResponse,
} from '@/models/linkShare'

export interface WebSocketCommandMap {
  // Auth
  'auth.login': { payload: { name: string; password: string }; response: { token: string; user: User } }

  'auth.register': {
    payload: { name: string; email: string; password: string; is_active?: boolean; role?: string }
    response: { token: string; user: User }
  }

  'auth.user.update': {
    payload: { id: number; name?: string; email?: string; password?: string; role?: string; is_active?: boolean }
    response: { user: User }
  }

  'auth.user.change_password': {
    payload: { id: number; old_password: string; new_password: string }
    response: { user: User }
  }

  'auth.isAuthenticated': { payload: { token: string }; response: { isAuthenticated: boolean; user?: User } }

  'auth.refresh': { payload: null; response: { token: string; user: User } }

  'auth.logout': { payload: null; response: { success: boolean } }

  'auth.me': { payload: null; response: { user: User } }

  'auth.users.list': { payload: null; response: { users: User[] } }

  'auth.user.get': { payload: { id: number }; response: { user: User } }

  'auth.user.get.byName': { payload: { name: string }; response: { user: User } }

  'auth.admin.default_password': { payload: null; response: { isDefault: boolean } }

  // Vault commands

  'storage.vault.list': { payload: null; response: { vaults: Vault[] } }

  'storage.vault.add': {
    payload:
      | { name: string; type: 'local'; mount_point: string }
      | { name: string; type: 's3'; api_key_id: number; bucket: string }
    response: { vault: LocalDiskVault | S3Vault }
  }

  'storage.vault.update': { payload: LocalDiskVault | S3Vault; response: { vault: LocalDiskVault | S3Vault } }

  'storage.vault.remove': { payload: { id: number }; response: null }

  'storage.vault.get': { payload: { id: number }; response: { vault: LocalDiskVault | S3Vault } }

  'storage.vault.sync': { payload: { id: number }; response: null }

  // API Key commands

  'storage.apiKey.list': { payload: null; response: { keys: string } }

  'storage.apiKey.list.user': { payload: null; response: { keys: string /* JSON string of API keys */ } }

  'storage.apiKey.add': { payload: Partial<S3APIKey>; response: null }

  'storage.apiKey.remove': { payload: { id: number }; response: null }

  'storage.apiKey.get': { payload: { id: number }; response: { api_key: APIKey } }

  // Roles and Permissions

  'role.admin.add': { payload: AdminRolePayload; response: { role: AdminRoleDTO } }

  'role.admin.update': { payload: AdminRolePayload; response: { role: AdminRoleDTO } }

  'role.admin.delete': { payload: { id: number }; response: { role: AdminRoleDTO } }

  'role.admin.get': { payload: { id: number }; response: { role: AdminRoleDTO } }

  'role.admin.get.byName': { payload: { name: string }; response: { role: AdminRoleDTO } }

  'roles.admin.list': { payload: null; response: { roles: AdminRoleDTO[] } }

  'role.vault.add': { payload: VaultRolePayload; response: { vault: VaultRoleDTO } }

  'role.vault.update': { payload: VaultRolePayload; response: { vault: VaultRoleDTO } }

  'role.vault.delete': { payload: { id: number }; response: { vault: VaultRoleDTO } }

  'role.vault.get': { payload: { id: number }; response: { vault: VaultRoleDTO } }

  'role.vault.get.byName': { payload: { name: string }; response: { vault: VaultRoleDTO } }

  'roles.vault.list': { payload: null; response: { roles: VaultRoleDTO[] } }

  'roles.vault.list.assigned': { payload: { id: number }; response: { vault: VaultRoleDTO } }

  'permission.get': { payload: { id: number }; response: { permission: Permission } }

  'permission.get.byName': { payload: { name: string }; response: { permission: Permission } }

  'permissions.list': { payload: null; response: { permissions: Permission[] } }

  // Settings
  'settings.get': { payload: null; response: { settings: Settings } }

  'settings.update': { payload: Partial<Settings>; response: { settings: Settings } }

  // Group commands

  'group.add': { payload: { name: string; description?: string }; response: { group: Group } }

  'group.remove': { payload: { id: number }; response: null }

  'group.update': { payload: Partial<Group>; response: { group: Group } }

  'group.get': { payload: { id: number }; response: { group: Group } }

  'groups.list': { payload: null; response: { groups: Group[] } }

  'group.member.add': { payload: { group_id: number; user_id: number }; response: { group: Group } }

  'group.member.remove': { payload: { group_id: number; user_id: number }; response: { group: Group } }

  'group.get.byName': { payload: { name: string }; response: { group: Group } }

  'group.get.byVolume': { payload: { volume_id: number }; response: { group: Group } }

  'group.volume.add': { payload: { group_id: number; volume_id: number }; response: { group: Group } }

  'group.volume.remove': { payload: { group_id: number; volume_id: number }; response: { group: Group } }

  'groups.list.byUser': { payload: { user_id: number }; response: { groups: Group[] } }

  'groups.list.byVolume': { payload: { volume_id: number }; response: { groups: Group[] } }

  // FS commands

  'fs.dir.create': { payload: { vault_id: number; path: string }; response: { path: string } }

  'fs.dir.list': {
    payload: { vault_id: number; path?: string | undefined }
    response: { vault: string; path: string; entry?: Directory; files: (File | Directory)[] }
  }

  'fs.metadata': {
    payload: { vault_id?: number | null; path?: string }
    response: { vault?: string; path: string; entry: File | Directory | ShareMetadataResponse['entry'] }
  }

  'fs.list': {
    payload: { vault_id?: number | null; path?: string }
    response: { vault?: string; path: string; entry?: File | Directory | ShareMetadataResponse['entry']; files: (File | Directory | ShareMetadataResponse['entry'])[] }
  }

  'fs.download.start': { payload: { path?: string; vault_id?: number | null }; response: ShareDownloadStartResponse }

  'fs.download.chunk': {
    payload: { transfer_id: string; offset: number; length?: number }
    response: ShareDownloadChunkResponse
  }

  'fs.download.cancel': { payload: { transfer_id: string }; response: ShareDownloadCancelResponse }

  'fs.upload.start': {
    payload:
      | IFileUpload
      | { path?: string; filename?: string; size_bytes?: number; size?: number; mime_type?: string | null; duplicate_policy?: 'reject' }
    response: { upload_id: string; transfer_id?: string; path?: string; filename?: string; size_bytes?: number; chunk_size?: number; duplicate_policy?: string }
  }

  'fs.upload.finish': { payload: Partial<IFileUpload> | { upload_id: string }; response: { path?: string } | ShareUploadFinishResponse }

  'fs.upload.cancel': { payload: { upload_id?: string }; response: { cancelled: boolean; upload_id?: string } }

  'fs.entry.delete': { payload: { vault_id: number; path: string }; response: null }

  'fs.entry.move': { payload: { vault_id: number; from: string; to: string }; response: { from: string; to: string } }

  'fs.entry.copy': { payload: { vault_id: number; from: string; to: string }; response: { from: string; to: string } }

  'fs.entry.rename': { payload: { vault_id: number; from: string; to: string }; response: { from: string; to: string } }

  // Share management commands

  'share.link.create': { payload: ShareLinkCreatePayload; response: ShareLinkTokenResponse }

  'share.link.get': { payload: { id: string }; response: ShareLinkResponse }

  'share.link.list': {
    payload: { vault_id?: number | null; limit?: number; offset?: number; page?: number; sort?: string; direction?: 'asc' | 'desc' }
    response: ShareLinkListResponse
  }

  'share.link.update': { payload: ShareLinkUpdatePayload; response: ShareLinkResponse }

  'share.link.revoke': { payload: { id: string }; response: { revoked: boolean } }

  'share.link.rotate_token': { payload: { id: string }; response: ShareLinkTokenResponse }

  // Public/share session commands

  'share.session.open': { payload: { public_token: string }; response: ShareSessionOpenResponse }

  'share.email.challenge.start': {
    payload: { email: string; public_token?: string; session_token?: string }
    response: ShareEmailChallengeStartResponse
  }

  'share.email.challenge.confirm': {
    payload: { challenge_id: string; code: string; session_id?: string; session_token?: string }
    response: ShareEmailChallengeConfirmResponse
  }

  // Ready share-mode filesystem and transfer commands

  'share.fs.metadata': { payload: { path?: string }; response: ShareMetadataResponse }

  'share.fs.list': { payload: { path?: string }; response: ShareListResponse }

  'share.download.start': { payload: { path?: string }; response: ShareDownloadStartResponse }

  'share.download.chunk': {
    payload: { transfer_id: string; offset: number; length?: number }
    response: ShareDownloadChunkResponse
  }

  'share.download.cancel': { payload: { transfer_id: string }; response: ShareDownloadCancelResponse }

  'share.preview.get': { payload: { path?: string; size?: number }; response: SharePreviewResponse }

  'share.upload.start': {
    payload: { path?: string; filename: string; size_bytes: number; mime_type?: string | null; duplicate_policy?: 'reject' }
    response: ShareUploadStartResponse
  }

  'share.upload.finish': { payload: { upload_id: string }; response: ShareUploadFinishResponse }

  'share.upload.cancel': { payload: { upload_id: string }; response: ShareUploadCancelResponse }

  // stats
  'stats.vault': { payload: { vault_id: number }; response: { stats: VaultStats } }

  'stats.fs.cache': { payload: null; response: { stats: CacheStats } }

  'stats.http.cache': { payload: null; response: { stats: CacheStats } }
}

export type WSCommandPayload<K extends keyof WebSocketCommandMap> = WebSocketCommandMap[K]['payload']
export type WSCommandResponse<K extends keyof WebSocketCommandMap> = WebSocketCommandMap[K]['response']
