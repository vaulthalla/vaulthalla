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
    response: { vault: string; path: string; files: (File | Directory)[] }
  }

  'fs.upload.start': { payload: IFileUpload; response: { upload_id: string } }

  'fs.upload.finish': { payload: Partial<IFileUpload>; response: { path: string } }

  'fs.entry.delete': { payload: { vault_id: number; path: string }; response: null }

  'fs.entry.move': { payload: { vault_id: number; from: string; to: string }; response: { from: string; to: string } }

  'fs.entry.copy': { payload: { vault_id: number; from: string; to: string }; response: { from: string; to: string } }

  'fs.entry.rename': { payload: { vault_id: number; from: string; to: string }; response: { from: string; to: string } }

  // stats
  'stats.vault': { payload: { vault_id: number }; response: { stats: VaultStats } }

  'stats.fs.cache': { payload: null; response: { stats: CacheStats } }

  'stats.http.cache': { payload: null; response: { stats: CacheStats } }
}

export type WSCommandPayload<K extends keyof WebSocketCommandMap> = WebSocketCommandMap[K]['payload']
export type WSCommandResponse<K extends keyof WebSocketCommandMap> = WebSocketCommandMap[K]['response']
