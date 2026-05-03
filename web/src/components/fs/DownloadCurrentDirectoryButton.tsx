'use client'

import DownloadIcon from '@/fa-duotone/download.svg'
import { useFSStore } from '@/stores/fsStore'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { hasEffectiveShareOperation } from '@/util/shareOperations'

export const DownloadCurrentDirectoryButton = () => {
  const mode = useFSStore(state => state.mode)
  const path = useFSStore(state => state.path)
  const currVault = useFSStore(state => state.currVault)
  const downloadFile = useFSStore(state => state.downloadFile)
  const share = useVaultShareStore(state => state.share)

  const isShareMode = mode === 'share'
  const canDownloadShareDirectory =
    isShareMode && share?.target_type === 'directory' && hasEffectiveShareOperation(share, 'download')
  const canDownloadAuthenticatedDirectory = mode === 'authenticated' && Boolean(currVault)
  const enabled = canDownloadShareDirectory || canDownloadAuthenticatedDirectory

  if (isShareMode && !canDownloadShareDirectory) return null

  const title = enabled ? 'Download current directory' : 'Current directory download is unavailable'

  return (
    <button
      className="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg border border-white/10 text-cyan-200 transition hover:bg-white/10 disabled:cursor-not-allowed disabled:opacity-45"
      type="button"
      title={title}
      aria-label={title}
      disabled={!enabled}
      onClick={() => downloadFile(path || '/').catch(err => console.error('Error downloading current directory:', err))}>
      <DownloadIcon className="h-4 w-4 fill-current" />
    </button>
  )
}
