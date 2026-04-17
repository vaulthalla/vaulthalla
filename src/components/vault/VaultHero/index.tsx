import Link from 'next/link'
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
  const assignButton = (
    <Link
      href={`/vaults/${vault.id}/assign`}
      className="inline-flex h-10 items-center justify-center rounded-md border border-cyan-400/30 bg-cyan-500/15 px-4 text-sm font-medium text-cyan-100 transition hover:bg-cyan-500/25">
      Assign User
    </Link>
  )

  return (
    <VaultHeroClient
      vault={vault}
      usedBytes={usedBytes}
      totalBytes={totalBytes}
      rightSlot={
        <>
          {assignButton}
          {rightSlot ?? null}
        </>
      }
    />
  )
}

export default VaultHero
