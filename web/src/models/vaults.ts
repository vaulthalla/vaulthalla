export type VaultType = 'local' | 's3'

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
  region: string
  access_key: string
  secret_access_key: string
  endpoint: string
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
  region: string = ''
  access_key: string = ''
  secret_access_key: string = ''
  endpoint: string = ''

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
