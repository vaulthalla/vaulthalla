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
import { canRequestSharePreview, hasEffectiveShareOperation } from '@/util/shareOperations'
import { buildPreviewUrl } from '@/util/previewUrl'
import { parseTimestamp } from '@/util/formatTimestamp'
import { buildDownloadUrl } from '@/util/downloadUrl'

type FsMode = 'authenticated' | 'share'
type FsEntry = DBFile | Directory
export interface UploadSuccessState {
  message: string
  filename: string | null
  previewUrl: string | null
  mimeType: string | null
  listUnavailable: boolean
}

interface FsStore {
  mode: FsMode
  currVault: Vault | LocalDiskVault | S3Vault | null
  path: string
  uploading: boolean
  uploadProgress: number
  uploadError: string | null
  uploadSuccess: UploadSuccessState | null
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
    canRequestSharePreview(shareState.share) && entry.path && entry.mime_type &&
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

const revokeUploadPreview = (uploadSuccess?: UploadSuccessState | null) => {
  if (typeof URL === 'undefined' || !uploadSuccess?.previewUrl?.startsWith('blob:')) return
  URL.revokeObjectURL(uploadSuccess.previewUrl)
}

const createUploadSuccess = (files: FileWithRelativePath[], listUnavailable: boolean): UploadSuccessState => {
  let previewFile: FileWithRelativePath | undefined
  if (listUnavailable) {
    for (let i = files.length - 1; i >= 0; i -= 1) {
      if (files[i].type.startsWith('image/')) {
        previewFile = files[i]
        break
      }
    }
  }
  const previewUrl = previewFile && typeof URL !== 'undefined' ? URL.createObjectURL(previewFile) : null

  return {
    message: uploadSuccessMessage(files),
    filename: files.length === 1 ? files[0].name : previewFile?.name ?? null,
    previewUrl,
    mimeType: previewFile?.type ?? null,
    listUnavailable,
  }
}

const clearTransferState = (uploadSuccess?: UploadSuccessState | null) => {
  revokeUploadPreview(uploadSuccess)
  return {
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
  }
}

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

const httpUploadConcurrency = () => {
  if (typeof navigator === 'undefined') return 4
  const cores = navigator.hardwareConcurrency || 4
  return Math.max(1, Math.min(4, Math.floor(cores / 2) || 1))
}

const httpResponseError = async (response: Response, fallback: string) => {
  const text = await response.text().catch(() => '')
  return new Error(text || fallback)
}

const runBounded = async <T,>(items: T[], limit: number, worker: (item: T) => Promise<void>) => {
  let next = 0
  const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (next < items.length) {
      const index = next
      next += 1
      await worker(items[index])
    }
  })
  await Promise.all(workers)
}

const joinUploadPath = (currentPath: string, relativePath: string) => {
  const base = currentPath || '/'
  return `${base}${base.endsWith('/') ? '' : '/'}${relativePath || ''}`
}

const authenticatedUploadTarget = (currentPath: string, relativePath: string) =>
  normalizeAuthPath(joinUploadPath(currentPath, relativePath))

interface HttpUploadSessionResponse {
  upload_id: string
  files: Array<{ file_id: string; path: string; size: number; transfer_id?: string }>
}

type PreparedUploadFile = {
  mode: 'authenticated'
  file: FileWithRelativePath
  fileId: string
  targetPath: string
} | {
  mode: 'share'
  file: FileWithRelativePath
  fileId: string
  targetPath: string
  shareTarget: { path: string; filename: string }
}

const uploadBody = async ({
  url,
  file,
  onProgress,
}: {
  url: string
  file: File
  onProgress: (bytes: number) => void
}) => new Promise<void>((resolve, reject) => {
  const xhr = new XMLHttpRequest()
  let observed = 0

  const finishProgress = () => {
    if (file.size > observed) {
      onProgress(file.size - observed)
      observed = file.size
    }
  }

  xhr.open('PUT', url)
  xhr.withCredentials = true
  xhr.setRequestHeader('Content-Type', file.type || 'application/octet-stream')
  xhr.upload.onprogress = event => {
    const loaded = Math.min(file.size, event.loaded)
    if (loaded > observed) {
      onProgress(loaded - observed)
      observed = loaded
    }
  }
  xhr.onload = () => {
    if (xhr.status >= 200 && xhr.status < 300) {
      finishProgress()
      resolve()
      return
    }
    reject(new Error(xhr.responseText || `Upload failed with HTTP ${xhr.status}`))
  }
  xhr.onerror = () => reject(new Error('Upload connection failed'))
  xhr.onabort = () => reject(new Error('Upload cancelled'))
  xhr.send(file)
})

const uploadHttpBatch = async ({
  files,
  mode,
  vault,
  currentPath,
  targetPaths,
  onProgress,
  onFileStart,
}: {
  files: FileWithRelativePath[]
  mode: FsMode
  vault: Vault | LocalDiskVault | S3Vault | null
  currentPath: string
  targetPaths?: string[]
  onProgress: (bytes: number) => void
  onFileStart: (filename: string) => void
}) => {
  if (mode === 'share') {
    requireReadyShareSession()
    await useShareWebSocketStore.getState().waitForConnection()
  } else if (!vault) throw new Error('No current vault selected')

  const uploadFiles: PreparedUploadFile[] = files.map((file, index) => {
    const relativePath = file.relativePath || file.name
    const requestedTargetPath = targetPaths?.[index]
    if (mode === 'share') {
      const target = shareUploadTarget(
        requestedTargetPath ? normalizeSharePath(requestedTargetPath) : normalizeSharePath(joinUploadPath(currentPath, relativePath)),
        currentPath,
        file.name,
      )
      return {
        mode: 'share',
        file,
        fileId: `f${index}`,
        shareTarget: target,
        targetPath: normalizeSharePath(joinUploadPath(target.path, target.filename)),
      }
    }
    return {
      mode: 'authenticated',
      file,
      fileId: `f${index}`,
      targetPath: requestedTargetPath ? normalizeAuthPath(requestedTargetPath) : authenticatedUploadTarget(currentPath, relativePath),
    }
  })

  const shareQuery = mode === 'share' ? '?share=1' : ''
  const start = await fetch(`/upload/session${shareQuery}`, {
    method: 'POST',
    credentials: 'same-origin',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      ...(mode === 'authenticated' ? { vault_id: vault?.id } : {}),
      files: uploadFiles.map(item => {
        const common = {
          file_id: item.fileId,
          mime_type: item.file.type || null,
          duplicate_policy: 'reject',
        }
        return item.mode === 'share'
          ? { ...common, path: item.shareTarget.path, filename: item.shareTarget.filename, size_bytes: item.file.size }
          : { ...common, path: item.targetPath, size: item.file.size }
      }),
    }),
  })
  if (!start.ok) throw await httpResponseError(start, 'Unable to start upload')
  const session = await start.json() as HttpUploadSessionResponse
  const uploadId = session.upload_id
  if (!uploadId) throw new Error('Upload session did not return an upload_id')

  try {
    await runBounded(uploadFiles, httpUploadConcurrency(), async ({ file, fileId }) => {
      onFileStart(file.name)
      await uploadBody({
        url: `/upload/${encodeURIComponent(uploadId)}/files/${encodeURIComponent(fileId)}${shareQuery}`,
        file,
        onProgress,
      })
    })

    const finish = await fetch(`/upload/${encodeURIComponent(uploadId)}/finish${shareQuery}`, {
      method: 'POST',
      credentials: 'same-origin',
    })
    if (!finish.ok) throw await httpResponseError(finish, 'Unable to finish upload')
  } catch (error) {
    await fetch(`/upload/${encodeURIComponent(uploadId)}${shareQuery}`, {
      method: 'DELETE',
      credentials: 'same-origin',
    }).catch(() => undefined)
    throw error
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
          currentDirectory: null,
          ...clearTransferState(get().uploadSuccess),
        })
      },

      exitShareMode() {
        set({
          mode: 'authenticated',
          path: '',
          files: [],
          currentDirectory: null,
          ...clearTransferState(get().uploadSuccess),
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
            set({ path: '', currentDirectory: null, files: [], ...clearTransferState(get().uploadSuccess) })
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
              set({ currVault: localVault, path: '', currentDirectory: null, files: [], ...clearTransferState(get().uploadSuccess) })
              await loadAuthenticatedDirectory(localVault, '')
              return
            }
          } catch (vaultError) {
            console.error('[FsStore] Vault recovery after list failure failed:', vaultError)
          }

          set({ currentDirectory: null, files: [], ...clearTransferState(get().uploadSuccess) })
          throw error
        }
      },

      async upload(files: FileWithRelativePath[]) {
        const { fetchFiles, path, mode, currVault } = get()

        set({ uploading: true, uploadProgress: 0, uploadError: null, uploadLabel: files.length === 1 ? files[0].name : `${files.length} files` })

        const totalBytes = files.reduce((sum, f) => sum + f.size, 0)
        let uploadedBytes = 0

        try {
          await uploadHttpBatch({
            files,
            mode,
            vault: currVault,
            currentPath: path,
            onFileStart: filename => set({ uploadLabel: filename }),
            onProgress: bytes => {
              uploadedBytes += bytes
              set({ uploadProgress: totalBytes > 0 ? Math.min(100, (uploadedBytes / totalBytes) * 100) : 100 })
            },
          })

          let listUnavailable = false

          if (get().mode === 'authenticated') {
            await useVaultStore.getState().syncVault({ id: get().currVault?.id || 0 })
            await fetchFiles()
          } else {
            const shareState = useVaultShareStore.getState()
            const canRelist = hasEffectiveShareOperation(shareState.share, 'list') || shareState.share?.target_type === 'file'
            listUnavailable = !canRelist
            if (canRelist)
              await fetchFiles()
          }

          const uploadSuccess = createUploadSuccess(files, listUnavailable)
          revokeUploadPreview(get().uploadSuccess)
          set({ uploadProgress: 100, uploadError: null, uploadSuccess })
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

      async uploadFile({ file, targetPath, onProgress }) {
        try {
          const { mode, currVault, path } = get()
          await uploadHttpBatch({
            files: [file as FileWithRelativePath],
            mode,
            vault: currVault,
            currentPath: path,
            targetPaths: targetPath ? [targetPath] : undefined,
            onFileStart: () => undefined,
            onProgress: bytes => onProgress?.(bytes),
          })
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
        set({ currVault: vault, path: '', currentDirectory: null, files: [], ...clearTransferState(get().uploadSuccess) })
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
                useFSStore.setState({ currVault: null, path: '', currentDirectory: null, files: [], ...clearTransferState(useFSStore.getState().uploadSuccess) })
                const localVault = await vaultStore.getLocalVault()
                if (localVault) await hydratedState.setCurrVault(localVault)
                else console.warn('[FsStore] No local vault found during rehydration')
              }

              if (!hydratedState.files || hydratedState.files.length === 0) await hydratedState.fetchFiles()
            } catch (err) {
              console.error('[FsStore] Rehydrate fetch failed:', err)
              useFSStore.setState({ currVault: null, path: '', currentDirectory: null, files: [], ...clearTransferState(useFSStore.getState().uploadSuccess) })
            }
          })()
        }
      },
    },
  ),
)
