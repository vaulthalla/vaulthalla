'use client'

import React, { useState } from 'react'
import FolderPlus from '@/fa-duotone-regular/folder-plus.svg'
import ShareIcon from '@/fa-duotone-regular/share-nodes.svg'
import { ShareManagementModal } from '@/components/share/ShareManagementModal'
import type { FilesystemRow } from '@/components/fs/types'
import { useFSStore } from '@/stores/fsStore'
import { Directory } from '@/models/directory'
import { formatTimestamp } from '@/util/formatTimestamp'

const joinPath = (base: string, name: string) => {
  const cleanName = name.trim().replace(/^\/+|\/+$/g, '')
  const prefix = !base || base === '/' ? '' : base.replace(/\/+$/g, '')
  return `${prefix}/${cleanName}`
}

const normalizePath = (value: string) => {
  if (!value) return '/'
  const withSlash = value.startsWith('/') ? value : `/${value}`
  const normalized = withSlash.replace(/\/+/g, '/')
  return normalized.length > 1 ? normalized.replace(/\/+$/g, '') : '/'
}

const parentPath = (value: string) => {
  const parts = normalizePath(value).split('/').filter(Boolean)
  parts.pop()
  return parts.length ? `/${parts.join('/')}` : '/'
}

export const FSRailActions = () => {
  const { currVault, currentDirectory, fetchFiles, listDirectory, mkdir, mode, path } = useFSStore()
  const [shareOpen, setShareOpen] = useState(false)
  const [shareTarget, setShareTarget] = useState<FilesystemRow | null>(null)
  const isAuthenticatedFs = mode === 'authenticated'
  const canUseCurrentDirectory = Boolean(currVault && isAuthenticatedFs)

  const openShareCurrentDirectory = async () => {
    if (!currVault || !isAuthenticatedFs) return
    let directory = useFSStore.getState().currentDirectory || currentDirectory
    if (!directory) {
      await fetchFiles().catch(() => undefined)
      directory = useFSStore.getState().currentDirectory
    }
    if (!directory?.id && normalizePath(path) !== '/') {
      const normalizedPath = normalizePath(path)
      const parentEntries = await listDirectory({ vault_id: currVault.id, path: parentPath(normalizedPath) }).catch(() => [])
      const hydrated = parentEntries.find(
        (entry): entry is Directory => entry instanceof Directory && normalizePath(entry.path || '') === normalizedPath,
      ) as Directory | undefined
      if (hydrated) directory = hydrated
    }
    if (!directory?.id) {
      window.alert('Current directory is still loading. Try again in a moment.')
      return
    }

    setShareTarget({
      ...directory,
      key: `${directory.vault_id}:${directory.path ?? directory.name}`,
      entryType: 'directory',
      size: 'Directory',
      modified: formatTimestamp(directory.updated_at),
      previewUrl: null,
    })
    setShareOpen(true)
  }

  const createDirectory = async () => {
    if (!currVault || !isAuthenticatedFs) return
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
        disabled={!currVault || !isAuthenticatedFs}
        onClick={createDirectory}>
        <FolderPlus className="h-5 w-5 fill-current" />
      </button>
      <button
        className="flex h-10 w-full items-center justify-center rounded-lg text-cyan-200 transition hover:bg-white/10 disabled:cursor-not-allowed disabled:opacity-40"
        title="Share Current Directory"
        aria-label="Share Current Directory"
        disabled={!canUseCurrentDirectory}
        onClick={openShareCurrentDirectory}>
        <ShareIcon className="h-5 w-5 fill-current" />
      </button>

      {shareOpen && currVault && shareTarget && (
        <ShareManagementModal target={shareTarget} vault={currVault} onClose={() => setShareOpen(false)} />
      )}
    </div>
  )
}
