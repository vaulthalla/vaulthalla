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
import { isPreviewableMime } from '@/util/previewMime'
import { useStatsStore } from '@/stores/statsStore'

type FsMode = 'authenticated' | 'share'
type FsEntry = DBFile | Directory

let transferTaskCounter = 0

const nextTransferTaskId = () => {
  transferTaskCounter += 1
  return `transfer-${Date.now()}-${transferTaskCounter}`
}

export interface UploadSuccessState {
  message: string
  filename: string | null
  previewUrl: string | null
  mimeType: string | null
  listUnavailable: boolean
}

export type TransferTaskType = 'upload' | 'download'
export type TransferTaskStatus = 'queued' | 'uploading' | 'finalizing' | 'syncing' | 'complete' | 'failed' | 'started'

export interface TransferTask {
  id: string
  type: TransferTaskType
  label: string
  status: TransferTaskStatus
  progress: number
  totalBytes?: number
  transferredBytes?: number
  startedAt?: number
  error: string | null
  createdAt: number
  updatedAt: number
}

interface FsStore {
  mode: FsMode
  currVault: Vault | LocalDiskVault | S3Vault | null
  path: string
  tasks: TransferTask[]
  uploading: boolean
  uploadProgress: number
  uploadError: string | null
  uploadErrorVaultId: number | null
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
  clearTransferTasks: () => void
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

const transferLabelFromPath = (path: string, fallback: string) => {
  const parts = path.split('/').filter(Boolean)
  return parts.at(-1) || fallback
}

const shareHttpPreviewUrl = (path?: string, size = 128) => {
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
    isPreviewableMime(entry.mime_type)) {
    ;(file as DBFile & { previewUrl?: string | null }).previewUrl = shareHttpPreviewUrl(entry.path, 128)
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

const fsEntryKey = (entry: FsEntry) => `${entry.vault_id}:${entry.path ?? entry.name}`

const optionalPreviewUrl = (entry: FsEntry) => (entry as DBFile & { previewUrl?: string | null }).previewUrl ?? null

const sameFsEntry = (a: FsEntry, b: FsEntry) => {
  const aIsDirectory = a instanceof Directory
  const bIsDirectory = b instanceof Directory
  if (aIsDirectory !== bIsDirectory) return false

  const shared =
    a.id === b.id &&
    a.vault_id === b.vault_id &&
    a.parent_id === b.parent_id &&
    a.name === b.name &&
    a.created_by === b.created_by &&
    a.created_at === b.created_at &&
    a.updated_at === b.updated_at &&
    a.last_modified_by === b.last_modified_by &&
    a.path === b.path &&
    a.size_bytes === b.size_bytes

  if (!shared) return false

  if (a instanceof Directory && b instanceof Directory)
    return a.file_count === b.file_count && a.subdirectory_count === b.subdirectory_count

  if (a instanceof DBFile && b instanceof DBFile)
    return a.mime_type === b.mime_type && optionalPreviewUrl(a) === optionalPreviewUrl(b)

  return true
}

const preserveFsEntry = <T extends FsEntry | null>(previous: T, next: T): T => {
  if (!previous || !next) return next
  return sameFsEntry(previous, next) ? previous : next
}

const mergeFsEntries = (previous: FsEntry[], next: FsEntry[]) => {
  const previousByKey = new Map(previous.map(entry => [fsEntryKey(entry), entry]))
  let changed = previous.length !== next.length
  const merged = next.map((entry, index) => {
    const previousEntry = previousByKey.get(fsEntryKey(entry))
    if (previousEntry && sameFsEntry(previousEntry, entry)) {
      if (previous[index] !== previousEntry) changed = true
      return previousEntry
    }

    changed = true
    return entry
  })

  return changed ? merged : previous
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

const isUploadQuotaError = (message: string) => {
  const normalized = message.toLowerCase()
  return normalized.includes('upload exceeds available vault storage') ||
    normalized.includes('share upload exceeds available vault storage') ||
    normalized.includes('upload exceeds vault quota')
}

const formatBytesValue = (bytes: number) => {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const index = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / (1024 ** index)
  return `${value.toFixed(value < 10 && index > 0 ? 1 : 0)} ${units[index]}`
}

const quotaUploadMessage = (vaultName: string, selectedBytes: number, availableBytes?: number | null) => (
  availableBytes == null
    ? `Upload exceeds vault quota for ${vaultName}. Selected ${formatBytesValue(selectedBytes)} is larger than available vault storage.`
    : `Upload exceeds vault quota for ${vaultName}. Selected ${formatBytesValue(selectedBytes)}; available ${formatBytesValue(availableBytes)}.`
)

const uploadErrorMessage = (error: unknown, fallback: string, mode: FsMode) => {
  const message = errorMessage(error, fallback)
  if (mode === 'share' && isDuplicateUploadTargetError(message)) return 'A file with that name already exists in this share.'
  if (isUploadQuotaError(message)) return 'Upload exceeds vault quota.'
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

const uploadQuotaPreflightError = async (
  mode: FsMode,
  currVault: Vault | LocalDiskVault | S3Vault | null,
  selectedBytes: number,
) => {
  if (mode !== 'authenticated' || !currVault || selectedBytes <= 0) return null

  try {
    const stats = await useStatsStore.getState().getVaultStorageBackendStats({ vault_id: currVault.id })
    const vaultStats = stats.vaults.find(vault => vault.vault_id === currVault.id) ?? stats.vaults[0]
    if (!vaultStats || vaultStats.quota_bytes <= 0 || vaultStats.free_space_bytes == null) return null
    if (selectedBytes <= vaultStats.free_space_bytes) return null

    return {
      message: quotaUploadMessage(currVault.name, selectedBytes, vaultStats.free_space_bytes),
      vaultId: currVault.id,
    }
  } catch (error) {
    console.debug('[FsStore] upload quota preflight unavailable:', error)
    return null
  }
}

const clearTransferState = (uploadSuccess?: UploadSuccessState | null) => {
  revokeUploadPreview(uploadSuccess)
  return {
    copiedItem: null,
    uploadProgress: 0,
    uploading: false,
    uploadError: null,
    uploadErrorVaultId: null,
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

const activeTransferStatuses: TransferTaskStatus[] = ['queued', 'uploading', 'finalizing', 'syncing']

const isActiveTransferTask = (task: TransferTask) => activeTransferStatuses.includes(task.status)

const trimTransferTasks = (tasks: TransferTask[]) => {
  const active = tasks.filter(isActiveTransferTask)
  const recent = tasks
    .filter(task => !isActiveTransferTask(task))
    .sort((a, b) => b.updatedAt - a.updatedAt)
    .slice(0, 8)

  return [...active, ...recent].sort((a, b) => {
    if (isActiveTransferTask(a) && !isActiveTransferTask(b)) return -1
    if (!isActiveTransferTask(a) && isActiveTransferTask(b)) return 1
    return b.updatedAt - a.updatedAt
  })
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

interface UploadTaskPayload {
  files: FileWithRelativePath[]
  mode: FsMode
  vault: Vault | LocalDiskVault | S3Vault | null
  currentPath: string
  targetPaths?: string[]
  onProgress?: (bytes: number) => void
  resolve?: () => void
  reject?: (error: unknown) => void
}

const nowMs = () => (typeof performance === 'undefined' ? Date.now() : performance.now())

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
  onFileComplete,
  onFinalizing,
}: {
  files: FileWithRelativePath[]
  mode: FsMode
  vault: Vault | LocalDiskVault | S3Vault | null
  currentPath: string
  targetPaths?: string[]
  onProgress: (bytes: number) => void
  onFileStart: (filename: string) => void
  onFileComplete?: (file: PreparedUploadFile) => void
  onFinalizing?: () => void
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
    await runBounded(uploadFiles, httpUploadConcurrency(), async uploadFile => {
      const { file, fileId } = uploadFile
      onFileStart(file.name)
      await uploadBody({
        url: `/upload/${encodeURIComponent(uploadId)}/files/${encodeURIComponent(fileId)}${shareQuery}`,
        file,
        onProgress,
      })
      try {
        onFileComplete?.(uploadFile)
      } catch (error) {
        console.debug('[FsStore] upload file completion hook failed:', error)
      }
    })

    onFinalizing?.()
    const finishStartedAt = nowMs()
    const finish = await fetch(`/upload/${encodeURIComponent(uploadId)}/finish${shareQuery}`, {
      method: 'POST',
      credentials: 'same-origin',
    })
    console.debug(`[FsStore] upload finish completed in ${Math.round(nowMs() - finishStartedAt)}ms`)
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
    (set, get) => {
      const uploadTaskPayloads = new Map<string, UploadTaskPayload>()
      let uploadRunnerActive = false

      const updateTransferTask = (id: string, patch: Partial<Omit<TransferTask, 'id' | 'type' | 'createdAt'>>) => {
        set(state => ({
          tasks: trimTransferTasks(state.tasks.map(task => (
            task.id === id ? { ...task, ...patch, updatedAt: Date.now() } : task
          ))),
        }))
      }

      const setListedDirectory = (next: { currentDirectory: Directory | null; files: FsEntry[]; path: string }) => {
        const latest = get()
        const currentDirectory = preserveFsEntry(latest.currentDirectory, next.currentDirectory)
        const files = mergeFsEntries(latest.files, next.files)
        if (latest.currentDirectory === currentDirectory && latest.files === files && latest.path === next.path) return
        set({ currentDirectory, files, path: next.path })
      }

      const hydrateCurrentUploadDirectory = async (payload: UploadTaskPayload, reason: string) => {
        const current = get()
        const uploadPath = payload.mode === 'share' ? normalizeSharePath(payload.currentPath) : normalizeAuthPath(payload.currentPath)

        if (payload.mode === 'share') {
          const shareState = useVaultShareStore.getState()
          const canList = hasEffectiveShareOperation(shareState.share, 'list')
          if (!canList || current.mode !== 'share' || normalizeSharePath(current.path) !== uploadPath) return

          const ws = useShareWebSocketStore.getState()
          await ws.waitForConnection()
          const response = await ws.sendCommand('fs.list', { path: uploadPath })
          const files = response.files.map(nativeEntryToFsEntry)
          const nextDirectory = response.entry ? nativeEntryToFsEntry(response.entry) : null
          const nextPath = normalizeSharePath(response.path ?? uploadPath)
          const latest = get()
          if (latest.mode !== 'share' || normalizeSharePath(latest.path) !== uploadPath || nextPath !== uploadPath) return

          setListedDirectory({
            currentDirectory: nextDirectory instanceof Directory ? nextDirectory : null,
            files,
            path: nextPath,
          })
          console.debug(`[FsStore] upload directory hydration (${reason}) merged ${files.length} share entries`)
          return
        }

        if (!payload.vault || current.mode !== 'authenticated' || current.currVault?.id !== payload.vault.id ||
          normalizeAuthPath(current.path) !== uploadPath) return

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()
        const response = await ws.sendCommand('fs.dir.list', { vault_id: payload.vault.id, path: uploadPath })
        const files = response.files.map(wireEntryToFsEntry)
        const nextPath = normalizeAuthPath(response.path ?? uploadPath)
        const nextDirectory = response.entry ? new Directory(response.entry) : inferListedDirectory(uploadPath, payload.vault, files)
        const latest = get()
        if (latest.mode !== 'authenticated' || latest.currVault?.id !== payload.vault.id ||
          normalizeAuthPath(latest.path) !== uploadPath || nextPath !== uploadPath) return

        setListedDirectory({ currentDirectory: nextDirectory, files, path: nextPath })
        console.debug(`[FsStore] upload directory hydration (${reason}) merged ${files.length} entries`)
      }

      const runUploadQueue = async () => {
        if (uploadRunnerActive) return
        uploadRunnerActive = true

        try {
          while (true) {
            const task = get().tasks.find(candidate => candidate.type === 'upload' && candidate.status === 'queued')
            if (!task) break

            const payload = uploadTaskPayloads.get(task.id)
            if (!payload) {
              updateTransferTask(task.id, { status: 'failed', error: 'Upload payload is no longer available' })
              continue
            }

            const totalBytes = payload.files.reduce((sum, file) => sum + file.size, 0)
            let uploadedBytes = 0
            const label = payload.files.length === 1 ? payload.files[0].name : `${payload.files.length} files`

            const hydrateUploadDirectorySafely = async (reason: string) => {
              try {
                await hydrateCurrentUploadDirectory(payload, reason)
              } catch (error) {
                console.debug(`[FsStore] upload directory hydration (${reason}) failed:`, error)
              }
            }

            updateTransferTask(task.id, {
              status: 'uploading',
              progress: 0,
              transferredBytes: 0,
              totalBytes,
              startedAt: Date.now(),
              error: null,
            })
            set({
              uploading: true,
              uploadProgress: 0,
              uploadError: null,
              uploadErrorVaultId: null,
              uploadLabel: label,
            })

            try {
              await uploadHttpBatch({
                files: payload.files,
                mode: payload.mode,
                vault: payload.vault,
                currentPath: payload.currentPath,
                targetPaths: payload.targetPaths,
                onFileStart: filename => set({ uploadLabel: filename }),
                onProgress: bytes => {
                  uploadedBytes += bytes
                  payload.onProgress?.(bytes)
                  const progress = totalBytes > 0 ? Math.min(100, (uploadedBytes / totalBytes) * 100) : 100
                  updateTransferTask(task.id, { progress, transferredBytes: uploadedBytes, totalBytes })
                  set({ uploadProgress: progress })
                },
                onFinalizing: () => {
                  updateTransferTask(task.id, { status: 'finalizing', progress: 100, transferredBytes: totalBytes, totalBytes })
                  set({ uploadProgress: 100, uploadLabel: label })
                },
              })
              await hydrateUploadDirectorySafely('finish')

              let listUnavailable = false
              if (payload.mode === 'share') {
                const shareState = useVaultShareStore.getState()
                const canRelist = hasEffectiveShareOperation(shareState.share, 'list')
                listUnavailable = !canRelist
              }

              const uploadSuccess = createUploadSuccess(payload.files, listUnavailable)
              revokeUploadPreview(get().uploadSuccess)
              updateTransferTask(task.id, { status: 'complete', progress: 100, transferredBytes: totalBytes, totalBytes, error: null })
              set({ uploadProgress: 100, uploading: false, uploadLabel: null, uploadError: null, uploadErrorVaultId: null, uploadSuccess })

              const refreshStartedAt = nowMs()
              try {
                if (payload.mode === 'authenticated') {
                  if (get().mode === 'authenticated' && get().currVault?.id === payload.vault?.id) {
                    await get().fetchFiles()
                  }
                } else if (!listUnavailable && get().mode === 'share') {
                  await get().fetchFiles()
                }
                console.debug(`[FsStore] post-upload refresh completed in ${Math.round(nowMs() - refreshStartedAt)}ms`)
              } catch (refreshError) {
                console.error('[FsStore] post-upload refresh failed:', refreshError)
              }
              payload.resolve?.()
            } catch (err) {
              console.error('[FsStore] upload task failed:', err)
              const rawMessage = errorMessage(err, 'Upload failed')
              const quotaVaultId = payload.mode === 'authenticated' && isUploadQuotaError(rawMessage) ? payload.vault?.id ?? null : null
              const message = quotaVaultId && payload.vault
                ? quotaUploadMessage(payload.vault.name, totalBytes)
                : uploadErrorMessage(err, 'Upload failed', payload.mode)
              updateTransferTask(task.id, { status: 'failed', error: message })
              set({ uploadError: message, uploadErrorVaultId: quotaVaultId })
              payload.reject?.(err)
            } finally {
              uploadTaskPayloads.delete(task.id)
              const hasQueuedUpload = get().tasks.some(candidate => candidate.type === 'upload' && candidate.status === 'queued')
              if (!hasQueuedUpload) set({ uploading: false, uploadLabel: null })
            }
          }
        } finally {
          uploadRunnerActive = false
          const hasPendingUpload = get().tasks.some(candidate => candidate.type === 'upload' && candidate.status === 'queued')
          if (hasPendingUpload) void runUploadQueue()
          else set({ uploading: false, uploadLabel: null })
        }
      }

      const enqueueUploadTask = async (
        files: FileWithRelativePath[],
        options: {
          targetPaths?: string[]
          onProgress?: (bytes: number) => void
          waitForCompletion?: boolean
        } = {},
      ) => {
        if (!files.length) return Promise.resolve()

        const { mode, currVault, path } = get()
        if (mode === 'authenticated' && !currVault) return Promise.reject(new Error('No current vault selected'))

        const id = nextTransferTaskId()
        const timestamp = Date.now()
        const totalBytes = files.reduce((sum, file) => sum + file.size, 0)
        const task: TransferTask = {
          id,
          type: 'upload',
          label: files.length === 1 ? files[0].name : `${files.length} files`,
          status: 'queued',
          progress: 0,
          totalBytes,
          transferredBytes: 0,
          error: null,
          createdAt: timestamp,
          updatedAt: timestamp,
        }

        const quotaError = await uploadQuotaPreflightError(mode, currVault, totalBytes)
        if (quotaError) {
          revokeUploadPreview(get().uploadSuccess)
          set(state => ({
            tasks: trimTransferTasks([{
              ...task,
              status: 'failed',
              error: quotaError.message,
              updatedAt: Date.now(),
            }, ...state.tasks]),
            uploadError: quotaError.message,
            uploadErrorVaultId: quotaError.vaultId,
            uploadSuccess: null,
            uploading: false,
            uploadLabel: null,
          }))
          return Promise.reject(new Error(quotaError.message))
        }

        let completion: Promise<void> | undefined
        const payload: UploadTaskPayload = {
          files,
          mode,
          vault: currVault,
          currentPath: path,
          targetPaths: options.targetPaths,
          onProgress: options.onProgress,
        }

        if (options.waitForCompletion) {
          completion = new Promise<void>((resolve, reject) => {
            payload.resolve = resolve
            payload.reject = reject
          })
        }

        revokeUploadPreview(get().uploadSuccess)
        uploadTaskPayloads.set(id, payload)
        set(state => ({
          tasks: trimTransferTasks([task, ...state.tasks]),
          uploadError: null,
          uploadErrorVaultId: null,
          uploadSuccess: null,
        }))
        void runUploadQueue()

        return completion ?? Promise.resolve()
      }

      return ({
      mode: 'authenticated',
      currVault: null,
      path: '',
      tasks: [],
      uploading: false,
      uploadProgress: 0,
      uploadError: null,
      uploadErrorVaultId: null,
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

      clearTransferTasks() {
        set(state => ({ tasks: state.tasks.filter(isActiveTransferTask) }))
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
            setListedDirectory({
              path: normalizeSharePath(response.path ?? normalizedPath),
              currentDirectory: currentDirectory instanceof Directory ? currentDirectory : null,
              files: entries,
            })
          } catch (error) {
            if (useVaultShareStore.getState().share?.target_type !== 'file') throw error
            const response = await ws.sendCommand('fs.metadata', { path: normalizedPath })
            setListedDirectory({
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
          setListedDirectory({ currentDirectory, files, path: response.path ?? requestedPath })
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
        await enqueueUploadTask(files)
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

        const id = nextTransferTaskId()
        const timestamp = Date.now()
        const label = transferLabelFromPath(downloadPath, 'Download')

        try {
          set(state => ({
            tasks: trimTransferTasks([{
              id,
              type: 'download',
              label,
              status: 'started',
              progress: 0,
              error: null,
              createdAt: timestamp,
              updatedAt: timestamp,
            }, ...state.tasks]),
            downloadError: null,
            downloadProgress: 0,
            downloading: false,
            downloadLabel: label,
          }))
          startBrowserDownload(url)
        } catch (error) {
          const message = errorMessage(error, 'Download failed')
          updateTransferTask(id, { status: 'failed', error: message })
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
          await enqueueUploadTask([file as FileWithRelativePath], {
            targetPaths: targetPath ? [targetPath] : undefined,
            onProgress,
            waitForCompletion: true,
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
      })
    },
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
