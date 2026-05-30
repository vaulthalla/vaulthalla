export type VaultType = 'local' | 's3'
export type S3BudgetPreset = 'conservative' | 'balanced' | 'bulk' | 'unlimited' | 'custom'
export type RemoteSyncStrategy = 'cache' | 'sync' | 'mirror'
export type RemoteConflictPolicy = 'keep_local' | 'keep_remote' | 'keep_newest' | 'ask'

export interface S3RequestBudget {
  list_requests: number | null
  head_requests: number | null
  get_requests: number | null
  put_requests: number | null
  copy_requests: number | null
  delete_requests: number | null
  downloaded_bytes: number | null
}

export interface RemoteSyncPolicy {
  id?: number
  vault_id?: number
  strategy: RemoteSyncStrategy
  conflict_policy: RemoteConflictPolicy
  interval: number | string
  enabled: boolean
  s3_request_budget: S3RequestBudget
  max_remote_index_age_seconds: number | null
  last_sync_at?: string
  last_success_at?: string
  created_at?: string
  updated_at?: string
}

interface IVault {
  id: number
  name: string
  type: VaultType
  owner_id: number
  owner: string
  is_active: boolean
  created_at: string
}

interface ILocalDisk extends IVault {
  mount_point: string
}

interface IS3 extends IVault {
  vault_id: number
  api_key_id: number
  bucket: string
  storage_tier_id?: string | null
  encrypt_upstream: boolean
  region: string
  access_key: string
  secret_access_key: string
  endpoint: string
  sync?: RemoteSyncPolicy
}

export class Vault implements IVault {
  id: number = 0
  name: string = ''
  type: VaultType = 'local'
  owner_id: number = 0
  owner: string = ''
  is_active: boolean = true
  created_at: string = new Date().toISOString()

  constructor(data?: Partial<IVault>) {
    if (data) Object.assign(this, data)
  }
}

export class LocalDiskVault implements ILocalDisk {
  id: number = 0
  name: string = ''
  type: VaultType = 'local'
  owner_id: number = 0
  owner: string = ''
  is_active: boolean = true
  created_at: string = new Date().toISOString()
  mount_point: string = ''

  constructor(data?: Partial<ILocalDisk>) {
    if (data) Object.assign(this, data)
  }
}

export class S3Vault implements IS3 {
  id: number = 0
  name: string = ''
  type: VaultType = 's3'
  owner_id: number = 0
  owner: string = ''
  is_active: boolean = true
  created_at: string = new Date().toISOString()
  vault_id: number = 0
  api_key_id: number = 0
  bucket: string = ''
  storage_tier_id: string | null = null
  encrypt_upstream: boolean = true
  region: string = ''
  access_key: string = ''
  secret_access_key: string = ''
  endpoint: string = ''
  sync?: RemoteSyncPolicy

  constructor(data?: Partial<IS3>) {
    if (data) Object.assign(this, data)
  }
}

export const getVaultType = (type: string) => {
  switch (type) {
    case 'local':
      return 'Local Disk Vault'
    case 's3':
      return 'S3 Vault'
    default:
      return 'Unknown Type'
  }
}
