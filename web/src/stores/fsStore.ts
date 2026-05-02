import { create } from 'zustand'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { File as DBFile } from '@/models/file'
import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'
import { persist } from 'zustand/middleware'
import { useVaultStore } from '@/stores/vaultStore'
import { FileWithRelativePath } from '@/models/systemFile'
import { Directory } from '@/models/directory'
import { useShareWebSocketStore } from '@/stores/useShareWebSocket'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { ShareEntry, SharePreviewResponse } from '@/models/linkShare'
import { hasShareOperation } from '@/util/shareOperations'
import { buildPreviewUrl } from '@/util/previewUrl'
import { parseTimestamp } from '@/util/formatTimestamp'
import { buildDownloadUrl } from '@/util/downloadUrl'

type FsMode = 'authenticated' | 'share'
type FsEntry = DBFile | Directory

interface FsStore {
  mode: FsMode
  currVault: Vault | LocalDiskVault | S3Vault | null
  path: string
  uploading: boolean
  uploadProgress: number
  uploadError: string | null
  uploadSuccess: string | null
  uploadLabel: string | null
  downloading: boolean
  downloadProgress: number
  downloadError: string | null
  downloadLabel: string | null
  previewing: boolean
  previewError: string | null
  sharePreview: SharePreviewResponse | null
  currentDirectory: Directory | null
  files: FsEntry[]
  copiedItem: FsEntry | null
  enterShareMode: () => void
  exitShareMode: () => void
  setCopiedItem: (item: FsEntry | null) => void
  pasteCopiedItem: (targetPath?: string) => Promise<void>
  fetchFiles: () => Promise<void>
  upload: (files: FileWithRelativePath[]) => Promise<void>
  uploadFile: ({
    file,
    targetPath,
    onProgress,
  }: {
    file: File
    targetPath?: string
    onProgress?: (bytes: number) => void
  }) => Promise<void>
  downloadFile: (path: string) => Promise<void>
  previewFile: (path: string) => Promise<SharePreviewResponse>
  clearSharePreview: () => void
  delete: (name: string) => Promise<void>
  mkdir: (payload: WSCommandPayload<'fs.dir.create'>) => Promise<void>
  move: (payload: WSCommandPayload<'fs.entry.move'>) => Promise<void>
  copy: (payload: WSCommandPayload<'fs.entry.copy'>) => Promise<void>
  rename: (payload: WSCommandPayload<'fs.entry.rename'>) => Promise<void>
  setCurrVault: (vault: Vault) => void
  setPath: (dir: string) => void
  listDirectory: (payload: WSCommandPayload<'fs.dir.list'>) => Promise<FsEntry[]>
}

const isShareRoute = () => typeof window !== 'undefined' && window.location.pathname.startsWith('/share/')

const normalizeSharePath = (value?: string) => {
  if (!value || value === '.') return '/'
  const raw = value.startsWith('/') ? value : `/${value}`
  const parts = raw.split('/').filter(Boolean)
  if (parts.some(part => part === '.' || part === '..')) throw new Error('Invalid share path')
  return parts.length ? `/${parts.join('/')}` : '/'
}

const parentPath = (path: string) => {
  const normalized = normalizeSharePath(path)
  const parts = normalized.split('/').filter(Boolean)
  parts.pop()
  return parts.length ? `/${parts.join('/')}` : '/'
}

const baseName = (path: string, fallback: string) => {
  const parts = normalizeSharePath(path).split('/').filter(Boolean)
  return parts.at(-1) || fallback
}

const shareHttpPreviewUrl = (path?: string, size = 64) => {
  if (!path) return null
  return buildPreviewUrl({ mode: 'share', path: normalizeSharePath(path), size })
}

const timestampToEpoch = (value?: string | number | null) => {
  const parsed = parseTimestamp(value)
  return parsed ? Math.floor(parsed.getTime() / 1000) : 0
}

const shareEntryToFsEntry = (entry: ShareEntry): FsEntry => {
  const common = {
    id: entry.id,
    vault_id: 0,
    name: entry.name,
    created_by: 0,
    created_at: timestampToEpoch(entry.created_at),
    updated_at: timestampToEpoch(entry.updated_at),
    path: entry.path,
    size_bytes: entry.size_bytes,
  }

  if (entry.type === 'directory') {
    return new Directory({
      ...common,
      file_count: entry.file_count ?? 0,
      subdirectory_count: entry.subdirectory_count ?? 0,
    })
  }

  const file = new DBFile({
    ...common,
    mime_type: entry.mime_type ?? undefined,
  })

  const shareState = useVaultShareStore.getState()
  if (shareState.status === 'ready' && shareState.sessionToken &&
    hasShareOperation(shareState.share?.allowed_ops, 'preview') && entry.path && entry.mime_type &&
    (entry.mime_type.startsWith('image/') || entry.mime_type === 'application/pdf')) {
    ;(file as DBFile & { previewUrl?: string | null }).previewUrl = shareHttpPreviewUrl(entry.path, 64)
  }

  return file
}

const isShareEntry = (entry: FsEntry | ShareEntry): entry is ShareEntry => 'type' in entry

const nativeEntryToFsEntry = (entry: FsEntry | ShareEntry): FsEntry => (
  isShareEntry(entry) ? shareEntryToFsEntry(entry) : wireEntryToFsEntry(entry)
)

const wireEntryToFsEntry = (entry: FsEntry): FsEntry => {
  if ('file_count' in entry || 'subdirectory_count' in entry || (entry as { type?: string }).type === 'directory')
    return new Directory(entry)
  return new DBFile(entry)
}

const normalizeAuthPath = (value?: string) => {
  if (!value) return '/'
  const withSlash = value.startsWith('/') ? value : `/${value}`
  const trimmed = withSlash.replace(/\/+/g, '/')
  return trimmed.length > 1 ? trimmed.replace(/\/+$/g, '') : '/'
}

const inferListedDirectory = (
  path: string,
  currVault: Vault | LocalDiskVault | S3Vault,
  files: FsEntry[],
): Directory | null => {
  const inferredId = files.find(entry => entry.parent_id !== undefined)?.parent_id
  if (!inferredId) return null

  const normalizedPath = normalizeAuthPath(path)
  const name = normalizedPath === '/' ? currVault.name : normalizedPath.split('/').filter(Boolean).at(-1) || currVault.name
  const now = Math.floor(Date.now() / 1000)

  return new Directory({
    id: inferredId,
    vault_id: currVault.id,
    name,
    path: normalizedPath,
    created_by: currVault.owner_id,
    created_at: now,
    updated_at: now,
    size_bytes: 0,
    file_count: files.filter(entry => entry instanceof DBFile).length,
    subdirectory_count: files.filter(entry => entry instanceof Directory).length,
  })
}

const shareUploadTarget = (targetPath: string | undefined, currentPath: string, filename: string) => {
  if (!targetPath || normalizeSharePath(targetPath) === normalizeSharePath(currentPath)) {
    return { path: normalizeSharePath(currentPath), filename }
  }

  const normalized = normalizeSharePath(targetPath)
  return {
    path: parentPath(normalized),
    filename: baseName(normalized, filename),
  }
}

const requireAuthenticatedMode = (mode: FsMode, action: string) => {
  if (mode === 'share') throw new Error(`${action} is not available in public share mode`)
}

const errorMessage = (error: unknown, fallback: string) => (error instanceof Error && error.message ? error.message : fallback)

const isDuplicateUploadTargetError = (message: string) => {
  const normalized = message.toLowerCase()
  return normalized.includes('share upload target already exists') ||
    normalized.includes('upload target already exists') ||
    normalized.includes('target already exists')
}

const uploadErrorMessage = (error: unknown, fallback: string, mode: FsMode) => {
  const message = errorMessage(error, fallback)
  if (mode === 'share' && isDuplicateUploadTargetError(message)) return 'A file with that name already exists in this share.'
  return message
}

const uploadSuccessMessage = (files: FileWithRelativePath[]) => {
  if (files.length === 1) return `${files[0].name} uploaded.`
  return `${files.length} files uploaded.`
}

const clearTransferState = () => ({
  copiedItem: null,
  uploadProgress: 0,
  uploading: false,
  uploadError: null,
  uploadSuccess: null,
  uploadLabel: null,
  downloadProgress: 0,
  downloading: false,
  downloadError: null,
  downloadLabel: null,
  previewing: false,
  previewError: null,
  sharePreview: null,
})

const startBrowserDownload = (url: string) => {
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.style.display = 'none'
  document.body.appendChild(anchor)
  anchor.click()
  anchor.remove()
}

const requireReadyShareSession = () => {
  const share = useVaultShareStore.getState()
  const ws = useShareWebSocketStore.getState()
  if (share.status !== 'ready' || !share.sessionToken) throw new Error('Share session is not ready')
  if (!ws.connected || !ws.socket || ws.socket.readyState !== WebSocket.OPEN)
    throw new Error('Share session disconnected. Reopen the share link and try again.')
}

const currentShareSocket = () => {
  requireReadyShareSession()
  const socket = useShareWebSocketStore.getState().socket
  if (!socket || socket.readyState !== WebSocket.OPEN) throw new Error('Share upload connection is not open')
  return socket
}

const waitForBufferedAmount = async (socket: WebSocket, maxBufferedBytes = 128 * 1024) => {
  while (socket.bufferedAmount > maxBufferedBytes) {
    await new Promise(resolve => window.setTimeout(resolve, 25))
    if (socket.readyState !== WebSocket.OPEN) throw new Error('Share upload connection closed')
  }
}

export const useFSStore = create<FsStore>()(
  persist(
    (set, get) => ({
      mode: 'authenticated',
      currVault: null,
      path: '',
      uploading: false,
      uploadProgress: 0,
      uploadError: null,
      uploadSuccess: null,
      uploadLabel: null,
      downloading: false,
      downloadProgress: 0,
      downloadError: null,
      downloadLabel: null,
      previewing: false,
      previewError: null,
      sharePreview: null,
      currentDirectory: null,
      files: [],
      copiedItem: null,

      enterShareMode() {
        set({
          mode: 'share',
          currVault: null,
          path: '/',
          files: [],
          copiedItem: null,
          uploadProgress: 0,
          uploading: false,
          uploadError: null,
          uploadSuccess: null,
          uploadLabel: null,
          downloadProgress: 0,
          downloading: false,
          downloadError: null,
          downloadLabel: null,
          previewing: false,
          previewError: null,
          sharePreview: null,
          currentDirectory: null,
        })
      },

      exitShareMode() {
        set({
          mode: 'authenticated',
          path: '',
          files: [],
          copiedItem: null,
          uploadProgress: 0,
          uploading: false,
          uploadError: null,
          uploadSuccess: null,
          uploadLabel: null,
          downloadProgress: 0,
          downloading: false,
          downloadError: null,
          downloadLabel: null,
          previewing: false,
          previewError: null,
          sharePreview: null,
          currentDirectory: null,
        })
      },

      setCopiedItem(item) {
        if (get().mode === 'share' && item) return
        set({ copiedItem: item })
      },

      async pasteCopiedItem(targetPath) {
        const { copiedItem, currVault, path, mode } = get()
        requireAuthenticatedMode(mode, 'Paste')
        if (!copiedItem || !currVault || !copiedItem.path) {
          console.warn('[FsStore] No item to paste or no current vault set')
          return
        }

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          const target = targetPath || path + '/' + copiedItem.name
          await ws.sendCommand('fs.entry.copy', { vault_id: currVault.id, from: copiedItem.path, to: target })
        } catch (error) {
          console.error('[FsStore] pasteCopiedItem error:', error)
          throw error
        } finally {
          set({ copiedItem: null })
          await get().fetchFiles()
        }
      },

      async fetchFiles() {
        const { mode, currVault, path } = get()

        if (mode === 'share') {
          const ws = useShareWebSocketStore.getState()
          requireReadyShareSession()
          await ws.waitForConnection()
          const normalizedPath = normalizeSharePath(path)

          try {
            const response = await ws.sendCommand('fs.list', { path: normalizedPath })
            const entries = response.files.map(nativeEntryToFsEntry)
            const currentDirectory = response.entry ? nativeEntryToFsEntry(response.entry) : null
            set({
              path: normalizeSharePath(response.path),
              currentDirectory: currentDirectory instanceof Directory ? currentDirectory : null,
              files: entries,
            })
          } catch (error) {
            if (useVaultShareStore.getState().share?.target_type !== 'file') throw error
            const response = await ws.sendCommand('fs.metadata', { path: normalizedPath })
            set({
              path: normalizeSharePath(response.path || normalizedPath),
              currentDirectory: null,
              files: [nativeEntryToFsEntry(response.entry)],
            })
          }
          return
        }

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        if (!currVault) {
          console.warn('[FsStore] No current vault to set')
          return
        }

        const loadAuthenticatedDirectory = async (vault: Vault | LocalDiskVault | S3Vault, requestedPath: string) => {
          const response = await ws.sendCommand('fs.dir.list', { vault_id: vault.id, path: requestedPath })
          const files = response.files.map(wireEntryToFsEntry)
          const currentDirectory = response.entry ? new Directory(response.entry) : inferListedDirectory(requestedPath, vault, files)
          set({ currentDirectory, files, path: response.path ?? requestedPath })
        }

        try {
          await loadAuthenticatedDirectory(currVault, path)
        } catch (error) {
          console.error('Error fetching files:', error)

          const normalizedPath = normalizeAuthPath(path)
          if (normalizedPath !== '/') {
            set({ path: '', currentDirectory: null, files: [], ...clearTransferState() })
            try {
              await loadAuthenticatedDirectory(currVault, '')
              return
            } catch (rootError) {
              console.error('[FsStore] Root fallback after stale path failed:', rootError)
            }
          }

          try {
            const vaultStore = useVaultStore.getState()
            await vaultStore.fetchVaults()
            const localVault = await vaultStore.getLocalVault()
            if (localVault) {
              set({ currVault: localVault, path: '', currentDirectory: null, files: [], ...clearTransferState() })
              await loadAuthenticatedDirectory(localVault, '')
              return
            }
          } catch (vaultError) {
            console.error('[FsStore] Vault recovery after list failure failed:', vaultError)
          }

          set({ currentDirectory: null, files: [], ...clearTransferState() })
          throw error
        }
      },

      async upload(files: FileWithRelativePath[]) {
        const { uploadFile, fetchFiles, path } = get()

        set({ uploading: true, uploadProgress: 0, uploadError: null, uploadLabel: files.length === 1 ? files[0].name : `${files.length} files` })

        const totalBytes = files.reduce((sum, f) => sum + f.size, 0)
        let uploadedBytes = 0

        try {
          for (const file of files) {
            set({ uploadLabel: file.name })
            await uploadFile({
              file,
              targetPath: `${path}/${file.relativePath}`,
              onProgress: bytes => {
                uploadedBytes += bytes
                set({ uploadProgress: totalBytes > 0 ? Math.min(100, (uploadedBytes / totalBytes) * 100) : 100 })
              },
            })
          }

          if (get().mode === 'authenticated') {
            await useVaultStore.getState().syncVault({ id: get().currVault?.id || 0 })
            await fetchFiles()
          } else {
            const shareState = useVaultShareStore.getState()
            if (hasShareOperation(shareState.share?.allowed_ops, 'list') || shareState.share?.target_type === 'file')
              await fetchFiles()
          }

          set({ uploadProgress: 100, uploadError: null, uploadSuccess: uploadSuccessMessage(files) })
        } catch (err) {
          console.error('[FsStore] upload() batch failed:', err)
          set({ uploadError: uploadErrorMessage(err, 'Upload failed', get().mode) })
          throw err
        } finally {
          set({ uploading: false, uploadLabel: null })
        }
      },

      async downloadFile(path) {
        const { currVault, mode } = get()
        const downloadPath = mode === 'share' ? normalizeSharePath(path) : (path || '/')
        const url = buildDownloadUrl({
          mode,
          path: downloadPath,
          vaultId: mode === 'authenticated' ? currVault?.id : null,
        })
        if (!url) throw new Error('Download target is not ready')

        try {
          set({ downloadError: null, downloadProgress: 0, downloading: false, downloadLabel: null })
          startBrowserDownload(url)
        } catch (error) {
          const message = errorMessage(error, 'Download failed')
          set({ downloading: false, downloadProgress: 0, downloadError: message })
          throw error
        }
      },

      async previewFile(path) {
        if (get().mode !== 'share') throw new Error('Authenticated previews use the existing preview route')

        const ws = useShareWebSocketStore.getState()
        requireReadyShareSession()
        await ws.waitForConnection()

        set({ previewing: true, previewError: null, sharePreview: null })
        try {
          const response = await ws.sendCommand('share.preview.get', { path: normalizeSharePath(path), size: 1024 })
          set({ previewing: false, sharePreview: response })
          return response
        } catch (error) {
          const message = errorMessage(error, 'Preview failed')
          set({ previewing: false, previewError: message, sharePreview: null })
          throw error
        }
      },

      clearSharePreview() {
        set({ sharePreview: null, previewError: null })
      },

      async uploadFile({ file, targetPath = get().path, onProgress }) {
        if (get().mode === 'share') {
          const ws = useShareWebSocketStore.getState()
          requireReadyShareSession()
          await ws.waitForConnection()

          const target = shareUploadTarget(targetPath, get().path, file.name)
          const startResp = await ws.sendCommand('fs.upload.start', {
            path: target.path,
            filename: target.filename,
            size_bytes: file.size,
            mime_type: file.type || null,
            duplicate_policy: 'reject',
          })

          const wsInstance = currentShareSocket()
          const chunkSize = startResp.chunk_size || 256 * 1024
          let offset = 0

          try {
            while (offset < file.size) {
              if (currentShareSocket() !== wsInstance)
                throw new Error('Share upload connection changed during upload. Reopen the share link and try again.')
              const slice = file.slice(offset, Math.min(offset + chunkSize, file.size))
              const arrayBuffer = await slice.arrayBuffer()
              if (wsInstance.readyState !== WebSocket.OPEN) throw new Error('Share upload connection closed')
              wsInstance.send(arrayBuffer)
              await waitForBufferedAmount(wsInstance)

              offset += slice.size
              onProgress?.(slice.size)
            }

            if (currentShareSocket() !== wsInstance)
              throw new Error('Share upload connection changed before finish. Reopen the share link and try again.')
            await ws.sendCommand('fs.upload.finish', { upload_id: startResp.upload_id })
          } catch (error) {
            await ws.sendCommand('fs.upload.cancel', { upload_id: startResp.upload_id }).catch(() => undefined)
            throw error
          }
          return
        }

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        const { currVault } = get()
        if (!currVault) throw new Error('No current vault selected')

        try {
          const startResp = await ws.sendCommand('fs.upload.start', {
            vault_id: currVault.id,
            path: targetPath,
            size: file.size,
          })

          const uploadId = startResp.upload_id
          if (!uploadId) throw new Error('No upload_id returned')

          const wsInstance = ws.socket
          if (!wsInstance || wsInstance.readyState !== WebSocket.OPEN) throw new Error('WebSocket is not connected')
          const chunkSize = 64 * 1024
          let offset = 0

          while (offset < file.size) {
            const slice = file.slice(offset, Math.min(offset + chunkSize, file.size))
            const arrayBuffer = await slice.arrayBuffer()
            wsInstance.send(arrayBuffer)

            offset += slice.size
            onProgress?.(slice.size)
          }

          await ws.sendCommand('fs.upload.finish', { vault_id: currVault.id, path: targetPath })
        } catch (err) {
          console.error('[FsStore] uploadFile error:', err)
          throw err
        }
      },

      async delete(name) {
        const { currVault, mode } = get()
        requireAuthenticatedMode(mode, 'Delete')
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        if (!currVault) {
          console.warn('[FsStore] No current vault set for deletion')
          return
        }

        const path = get().path + '/' + name

        try {
          await ws.sendCommand('fs.entry.delete', { vault_id: currVault.id, path })
          await get().fetchFiles()
        } catch (error) {
          console.error('Error deleting:', error)
          throw error
        }
      },

      async mkdir({ vault_id, path }) {
        requireAuthenticatedMode(get().mode, 'Mkdir')
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.dir.create', { vault_id, path })
          await get().fetchFiles()
        } catch (error) {
          console.error('Error creating directory:', error)
          throw error
        }
      },

      async move({ vault_id, from, to }) {
        requireAuthenticatedMode(get().mode, 'Move')
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.entry.move', { vault_id, from, to })
          await get().fetchFiles()
        } catch (error) {
          console.error('Error moving file:', error)
          throw error
        }
      },

      async copy({ vault_id, from, to }) {
        requireAuthenticatedMode(get().mode, 'Copy')
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.entry.copy', { vault_id, from, to })
          await get().fetchFiles()
        } catch (error) {
          console.error('Error copying file:', error)
          throw error
        }
      },

      async rename({ vault_id, from, to }) {
        requireAuthenticatedMode(get().mode, 'Rename')
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.entry.rename', { vault_id, from, to })
          await get().fetchFiles()
        } catch (error) {
          console.error('Error renaming file:', error)
          throw error
        }
      },

      async setCurrVault(vault) {
        requireAuthenticatedMode(get().mode, 'Vault selection')
        set({ currVault: vault, path: '', currentDirectory: null, files: [], ...clearTransferState() })
        await get().fetchFiles()
      },

      async setPath(dir) {
        const path = get().mode === 'share' ? normalizeSharePath(dir) : dir
        set({ path })
        await get().fetchFiles()
      },

      async listDirectory({ vault_id, path = get().path }) {
        if (get().mode === 'share') {
          const ws = useShareWebSocketStore.getState()
          await ws.waitForConnection()
          const response = await ws.sendCommand('fs.list', { path: normalizeSharePath(path) })
          return response.files.map(nativeEntryToFsEntry)
        }

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          const response = await ws.sendCommand('fs.dir.list', { vault_id, path })
          return response.files
        } catch (error) {
          console.error('Error listing directory:', error)
          throw error
        }
      },
    }),
    {
      name: 'vaulthalla-fs',
      partialize: state => ({
        currVault: state.mode === 'authenticated' ? state.currVault : null,
        path: state.mode === 'authenticated' ? state.path : '',
      }),
      onRehydrateStorage: () => {
        return hydratedState => {
          if (!hydratedState || isShareRoute()) return
          console.log('[FsStore] Rehydrated from storage')
          ;(async () => {
            try {
              await useWebSocketStore.getState().waitForConnection()

              const vaultStore = useVaultStore.getState()
              await vaultStore.fetchVaults()
              const freshVault = hydratedState.currVault ?
                useVaultStore.getState().vaults.find((v: Vault) => v.id === hydratedState.currVault?.id)
              : undefined

              if (freshVault) {
                await hydratedState.setCurrVault(freshVault)
              } else {
                useFSStore.setState({ currVault: null, path: '', currentDirectory: null, files: [], ...clearTransferState() })
                const localVault = await vaultStore.getLocalVault()
                if (localVault) await hydratedState.setCurrVault(localVault)
                else console.warn('[FsStore] No local vault found during rehydration')
              }

              if (!hydratedState.files || hydratedState.files.length === 0) await hydratedState.fetchFiles()
            } catch (err) {
              console.error('[FsStore] Rehydrate fetch failed:', err)
              useFSStore.setState({ currVault: null, path: '', currentDirectory: null, files: [], ...clearTransferState() })
            }
          })()
        }
      },
    },
  ),
)
