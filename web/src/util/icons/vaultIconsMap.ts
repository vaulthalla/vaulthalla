import HardDrive from '@/fa-duotone-regular/hard-drive.svg'
import Cloud from '@/fa-duotone/cloud.svg'
import AWS from '@/fa-brands/aws.svg'
import Cloudflare from '@/fa-brands/cloudflare.svg'
import { Vault } from '@/models/vaults'
import { APIKey } from '@/models/apiKey'

export const getVaultIcon = ({ type, provider }: { type: Vault['type']; provider: APIKey['provider'] }) => {
  switch (type) {
    case 'local':
      return HardDrive
    case 's3':
      if (provider === 'aws') return AWS
      else if (provider === 'cloudflare') return Cloudflare
      return Cloud
    default:
      return HardDrive
  }
}
