import { ReactNode } from 'react'
import { Vault } from '@/models/vaults'
import VaultHeroClient from '@/components/vault/VaultHero/index.client'

type VaultHeroProps = {
  vault: Vault
  rightSlot?: ReactNode
  usedBytes?: number
  totalBytes?: number
}

const VaultHero = ({ vault, rightSlot, usedBytes, totalBytes }: VaultHeroProps) => {
  return (
    <VaultHeroClient
      vault={vault}
      usedBytes={usedBytes}
      totalBytes={totalBytes}
      rightSlot={rightSlot}
    />
  )
}

export default VaultHero
