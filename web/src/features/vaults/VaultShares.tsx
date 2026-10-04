'use client'

import React from 'react'
import Link from 'next/link'
import { Button } from '@/components/ui/Button'
import { FolderOpenIcon } from '@/components/ui/icons'
import { ShareLinkList } from '@/features/shares/ShareLinks'
import { useCurrentVault } from '@/features/vaults/VaultShell'

export const VaultShares = () => {
  const vault = useCurrentVault()
  return (
    <div className="space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <p className="max-w-2xl text-sm text-fg-subtle">
          Public links into this vault. New links are made from a file or folder in the file browser.
        </p>
        <Button asChild variant="subtle">
          <Link href={`/files/${vault.id}`}>
            <FolderOpenIcon aria-hidden />
            Share from files
          </Link>
        </Button>
      </div>
      <ShareLinkList vaultId={vault.id} emptyText="No share links into this vault yet." />
    </div>
  )
}
