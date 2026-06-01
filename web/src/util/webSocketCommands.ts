import { LocalDiskVault, RemoteSyncPolicy, S3Vault, Vault } from '@/models/vaults'
import { VaultStats } from '@/models/stats/vaultStats'
import { VaultActivity } from '@/models/stats/vaultActivity'
import { VaultRecovery } from '@/models/stats/vaultRecovery'
import { VaultSecurity } from '@/models/stats/vaultSecurity'
import { VaultShareStats } from '@/models/stats/vaultShareStats'
import { VaultSyncHealth } from '@/models/stats/vaultSyncHealth'
import { APIKey, S3APIKey } from '@/models/apiKey'
import { User } from '@/models/user'
import { AdminRolePayload, VaultRolePayload, Permission } from '@/models/role'
import { Settings } from '@/models/settings'
import { Group } from '@/models/group'
import { File, IFileUpload } from '@/models/file'
import { Directory } from '@/models/directory'
import {
  DashboardPreference,
  DashboardPreferencePayload,
  DashboardPreferenceUpdatePayload,
} from '@/models/dashboard/dashboardPreferences'
import { CacheStats } from '@/models/stats/cacheStats'
import { ConnectionStats } from '@/models/stats/connectionStats'
import { DashboardOverview, DashboardOverviewRequest } from '@/models/stats/dashboardOverview'
import { DbStats } from '@/models/stats/dbStats'
import { FuseStats } from '@/models/stats/fuseStats'
import { OperationStats } from '@/models/stats/operationStats'
import { RetentionStats } from '@/models/stats/retentionStats'
import { StatsTrends } from '@/models/stats/statsTrends'
import { StorageBackendStats } from '@/models/stats/storageBackendStats'
import { SystemHealth } from '@/models/stats/systemHealth'
import { ThreadPoolManagerStats } from '@/models/stats/threadPoolStats'
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
import {
  OperatorEmailConfigPatch,
  OperatorEmailConfigResponse,
  OperatorEmailHistoryRecord,
  OperatorEmailSecretPayload,
  OperatorEmailTestPayload,
  OperatorEmailTestResponse,
} from '@/models/operatorEmail'
import {
  PriceBudgetPolicy,
  PriceBudgetPolicyPayload,
  PriceBudgetPreflightPayload,
  PriceBudgetPreflightResult,
  PriceBudgetScope,
  PriceBudgetStatus,
} from '@/models/pricing/priceBudget'
import { PriceBudgetLedgerEntry } from '@/models/pricing/priceBudgetLedger'
import { PriceNotification } from '@/models/pricing/priceNotification'
import { PriceOverride, PriceOverrideRequestPayload } from '@/models/pricing/priceOverride'
import { PricingBudgetStats } from '@/models/stats/pricingBudgetStats'
import {
  S3GatewayBucketBinding,
  S3GatewayBucketBindPayload,
  S3GatewayCreateLocalBucketPayload,
  S3GatewayCreateRemoteCachePayload,
  S3GatewayCredential,
  S3GatewayCredentialCreatePayload,
  S3GatewayCredentialScopeUpdatePayload,
  S3GatewayCredentialVaultScope,
  S3GatewayStatus,
} from '@/models/s3Gateway'

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
      | {
          name: string
          type: 's3'
          api_key_id: number
          bucket: string
          storage_tier_id?: string | null
          encrypt_upstream?: boolean
          sync?: RemoteSyncPolicy
        }
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

  // Operator email administration
  'email.config.get': { payload: null; response: OperatorEmailConfigResponse }

  'email.config.update': { payload: OperatorEmailConfigPatch; response: OperatorEmailConfigResponse }

  'email.provider.secret.set': { payload: OperatorEmailSecretPayload; response: { secrets: OperatorEmailConfigResponse['secrets'] } }

  'email.test.send': { payload: OperatorEmailTestPayload; response: OperatorEmailTestResponse }

  'email.history': { payload: { limit?: number } | null; response: { history: OperatorEmailHistoryRecord[] } }

  // S3 price budget command center
  'pricing.budget.policy.list': {
    payload: { vault_id?: number | null; gateway_credential_id?: number | null; include_inactive?: boolean } | null
    response: { policies: PriceBudgetPolicy[] }
  }

  'pricing.budget.policy.upsert': { payload: PriceBudgetPolicyPayload; response: { policy: PriceBudgetPolicy } }

  'pricing.budget.policy.disable': {
    payload: { scope: PriceBudgetScope; provider_key?: string | null; vault_id?: number | null; gateway_credential_id?: number | null }
    response: { disabled: boolean }
  }

  'pricing.budget.ledger.list': {
    payload: { vault_id?: number | null; gateway_credential_id?: number | null; limit?: number } | null
    response: { ledger: PriceBudgetLedgerEntry[] }
  }

  'pricing.budget.status': {
    payload: { vault_id?: number | null; gateway_credential_id?: number | null; limit?: number; include_inactive?: boolean } | null
    response: PriceBudgetStatus
  }

  'pricing.budget.preflight': { payload: PriceBudgetPreflightPayload; response: PriceBudgetPreflightResult }

  'pricing.budget.override.request': { payload: PriceOverrideRequestPayload; response: { override: PriceOverride } }

  'pricing.budget.override.approve': { payload: { id: number }; response: { override: PriceOverride } }

  'pricing.budget.override.deny': { payload: { id: number; reason?: string | null }; response: { override: PriceOverride } }

  'pricing.budget.override.list': {
    payload: { vault_id?: number | null; limit?: number; include_expired?: boolean } | null
    response: { overrides: PriceOverride[] }
  }

  'pricing.notifications.list': {
    payload: { vault_id?: number | null; limit?: number; include_acknowledged?: boolean } | null
    response: { notifications: PriceNotification[] }
  }

  'pricing.notifications.ack': {
    payload: { id: number; vault_id?: number | null }
    response: { notification: PriceNotification }
  }

  // S3 gateway management

  's3.gateway.status': { payload: null; response: { status: S3GatewayStatus } }

  's3.gateway.credentials.create': {
    payload: S3GatewayCredentialCreatePayload
    response: { credential: S3GatewayCredential; secret_access_key: string }
  }

  's3.gateway.credentials.list': {
    payload: { include_disabled?: boolean } | null
    response: { credentials: S3GatewayCredential[] }
  }

  's3.gateway.credentials.revoke': {
    payload: { access_key?: string; name?: string }
    response: { revoked: boolean }
  }

  's3.gateway.credentials.scope.update': {
    payload: S3GatewayCredentialScopeUpdatePayload
    response: { credential: S3GatewayCredential | null }
  }

  's3.gateway.credentials.scope.list': {
    payload: { access_key?: string; name?: string }
    response: { credential: S3GatewayCredential; scopes: S3GatewayCredentialVaultScope[] }
  }

  's3.gateway.buckets.list': { payload: null; response: { buckets: S3GatewayBucketBinding[] } }

  's3.gateway.buckets.bind': { payload: S3GatewayBucketBindPayload; response: { bound: boolean } }

  's3.gateway.buckets.unbind': { payload: { bucket_name: string }; response: { unbound: boolean } }

  's3.gateway.buckets.createLocal': {
    payload: S3GatewayCreateLocalBucketPayload
    response: { bucket: S3GatewayBucketBinding }
  }

  's3.gateway.buckets.createRemoteCache': {
    payload: S3GatewayCreateRemoteCachePayload
    response: { bucket: S3GatewayBucketBinding }
  }

  's3.gateway.budget.policy.list': {
    payload: { gateway_credential_id?: number | null; vault_id?: number | null; include_inactive?: boolean } | null
    response: { policies: PriceBudgetPolicy[] }
  }

  's3.gateway.budget.policy.upsert': { payload: PriceBudgetPolicyPayload; response: { policy: PriceBudgetPolicy } }

  's3.gateway.budget.policy.disable': { payload: PriceBudgetPolicyPayload; response: { disabled: boolean } }

  's3.gateway.budget.ledger.list': {
    payload: { gateway_credential_id?: number | null; vault_id?: number | null; limit?: number } | null
    response: { ledger: PriceBudgetLedgerEntry[] }
  }

  's3.gateway.budget.status': {
    payload: { gateway_credential_id?: number | null; vault_id?: number | null; limit?: number } | null
    response: PriceBudgetStatus
  }

  // Dashboard preferences

  'dashboard.preferences.get': { payload: DashboardPreferencePayload | null; response: { preferences: DashboardPreference } }

  'dashboard.preferences.update': { payload: DashboardPreferenceUpdatePayload; response: { preferences: DashboardPreference } }

  'dashboard.preferences.reset': { payload: DashboardPreferencePayload | null; response: { reset: boolean; deleted?: boolean } }

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

  'stats.vault.sync': { payload: { vault_id: number }; response: { stats: VaultSyncHealth } }

  'stats.vault.activity': { payload: { vault_id: number }; response: { stats: VaultActivity } }

  'stats.vault.shares': { payload: { vault_id: number }; response: { stats: VaultShareStats } }

  'stats.vault.recovery': { payload: { vault_id: number }; response: { stats: VaultRecovery } }

  'stats.vault.operations': { payload: { vault_id: number }; response: { stats: OperationStats } }

  'stats.vault.security': { payload: { vault_id: number }; response: { stats: VaultSecurity } }

  'stats.vault.storage': { payload: { vault_id: number }; response: { stats: StorageBackendStats } }

  'stats.vault.retention': { payload: { vault_id: number }; response: { stats: RetentionStats } }

  'stats.vault.trends': { payload: { vault_id: number; window_hours?: number }; response: { stats: StatsTrends } }

  'stats.vault.pricing': { payload: { vault_id: number }; response: { stats: PricingBudgetStats } }

  'stats.dashboard.overview': { payload: DashboardOverviewRequest | null; response: { stats: DashboardOverview } }

  'stats.pricing.budget': { payload: { vault_id?: number | null } | null; response: { stats: PricingBudgetStats } }

  'stats.system.health': { payload: null; response: { stats: SystemHealth } }

  'stats.system.threadpools': { payload: null; response: { stats: ThreadPoolManagerStats } }

  'stats.system.fuse': { payload: null; response: { stats: FuseStats } }

  'stats.system.db': { payload: null; response: { stats: DbStats } }

  'stats.system.operations': { payload: null; response: { stats: OperationStats } }

  'stats.system.connections': { payload: null; response: { stats: ConnectionStats } }

  'stats.system.storage': { payload: null; response: { stats: StorageBackendStats } }

  'stats.system.retention': { payload: null; response: { stats: RetentionStats } }

  'stats.system.trends': { payload: { window_hours?: number }; response: { stats: StatsTrends } }

  'stats.system.pricing': { payload: null; response: { stats: PricingBudgetStats } }

  'stats.fs.cache': { payload: null; response: { stats: CacheStats } }

  'stats.http.cache': { payload: null; response: { stats: CacheStats } }
}

export type WSCommandPayload<K extends keyof WebSocketCommandMap> = WebSocketCommandMap[K]['payload']
export type WSCommandResponse<K extends keyof WebSocketCommandMap> = WebSocketCommandMap[K]['response']
