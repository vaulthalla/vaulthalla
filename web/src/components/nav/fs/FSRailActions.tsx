'use client'

import React, { useMemo, useState } from 'react'
import FolderPlus from '@/fa-duotone-regular/folder-plus.svg'
import ShareIcon from '@/fa-duotone-regular/share-nodes.svg'
import { ShareManagementModal } from '@/components/share/ShareManagementModal'
import type { FilesystemRow } from '@/components/fs/types'
import { useFSStore } from '@/stores/fsStore'

const joinPath = (base: string, name: string) => {
  const cleanName = name.trim().replace(/^\/+|\/+$/g, '')
  const prefix = !base || base === '/' ? '' : base.replace(/\/+$/g, '')
  return `${prefix}/${cleanName}`
}

export const FSRailActions = () => {
  const { currVault, currentDirectory, mkdir, path } = useFSStore()
  const [shareOpen, setShareOpen] = useState(false)
  const canUseCurrentDirectory = Boolean(currVault && currentDirectory)

  const shareTarget = useMemo<FilesystemRow | null>(() => {
    if (!currentDirectory) return null
    return {
      ...currentDirectory,
      key: `${currentDirectory.vault_id}:${currentDirectory.path ?? currentDirectory.name}`,
      entryType: 'directory',
      size: 'Directory',
      modified: new Date(currentDirectory.updated_at).toLocaleString(),
      previewUrl: null,
    }
  }, [currentDirectory])

  const createDirectory = async () => {
    if (!currVault) return
    const name = window.prompt('Directory name')
    if (!name?.trim()) return
    await mkdir({ vault_id: currVault.id, path: joinPath(path, name) }).catch(err => {
      window.alert(err instanceof Error ? err.message : 'Unable to create directory')
    })
  }

  return (
    <div className="flex w-[85%] flex-col gap-2 rounded-xl border border-white/10 bg-black/20 p-2">
      <button
        className="flex h-10 w-full items-center justify-center rounded-lg text-cyan-200 transition hover:bg-white/10 disabled:cursor-not-allowed disabled:opacity-40"
        title="Create Directory"
        aria-label="Create Directory"
        disabled={!currVault}
        onClick={createDirectory}>
        <FolderPlus className="h-5 w-5 fill-current" />
      </button>
      <button
        className="flex h-10 w-full items-center justify-center rounded-lg text-cyan-200 transition hover:bg-white/10 disabled:cursor-not-allowed disabled:opacity-40"
        title="Share Current Directory"
        aria-label="Share Current Directory"
        disabled={!canUseCurrentDirectory}
        onClick={() => setShareOpen(true)}>
        <ShareIcon className="h-5 w-5 fill-current" />
      </button>

      {shareOpen && currVault && shareTarget && (
        <ShareManagementModal target={shareTarget} vault={currVault} onClose={() => setShareOpen(false)} />
      )}
    </div>
  )
}
