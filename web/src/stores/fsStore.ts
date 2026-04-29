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
import { ShareEntry } from '@/models/linkShare'

type FsMode = 'authenticated' | 'share'
type FsEntry = DBFile | Directory

interface FsStore {
  mode: FsMode
  currVault: Vault | LocalDiskVault | S3Vault | null
  path: string
  uploading: boolean
  uploadProgress: number
  uploadError: string | null
  uploadLabel: string | null
  downloading: boolean
  downloadProgress: number
  downloadError: string | null
  downloadLabel: string | null
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

const timestampToEpoch = (value?: string) => {
  if (!value) return 0
  const parsed = Date.parse(value)
  return Number.isNaN(parsed) ? 0 : Math.floor(parsed / 1000)
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

  return new DBFile({
    ...common,
    mime_type: entry.mime_type ?? undefined,
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

const decodeBase64 = (value: string): Uint8Array<ArrayBuffer> => {
  const binary = window.atob(value)
  const bytes = new Uint8Array(new ArrayBuffer(binary.length))
  for (let i = 0; i < binary.length; i += 1) bytes[i] = binary.charCodeAt(i)
  return bytes
}

const safeDownloadName = (filename: string) => {
  const cleaned = filename.replace(/[\/\\\u0000]/g, '_').trim()
  return cleaned && cleaned !== '.' && cleaned !== '..' ? cleaned : 'download'
}

const saveBrowserDownload = (filename: string, chunks: Uint8Array<ArrayBuffer>[], mimeType?: string | null) => {
  const blob = new Blob(chunks, { type: mimeType || 'application/octet-stream' })
  const url = window.URL.createObjectURL(blob)
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = safeDownloadName(filename)
  anchor.style.display = 'none'
  document.body.appendChild(anchor)
  anchor.click()
  anchor.remove()
  window.URL.revokeObjectURL(url)
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
      uploadLabel: null,
      downloading: false,
      downloadProgress: 0,
      downloadError: null,
      downloadLabel: null,
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
          uploadLabel: null,
          downloadProgress: 0,
          downloading: false,
          downloadError: null,
          downloadLabel: null,
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
          uploadLabel: null,
          downloadProgress: 0,
          downloading: false,
          downloadError: null,
          downloadLabel: null,
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
          await ws.waitForConnection()
          const response = await ws.sendCommand('share.fs.list', { path: normalizeSharePath(path) })
          set({ path: normalizeSharePath(response.path), files: response.entries.map(shareEntryToFsEntry) })
          return
        }

        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        if (!currVault) {
          console.warn('[FsStore] No current vault to set')
          return
        }

        try {
          const response = await ws.sendCommand('fs.dir.list', { vault_id: currVault.id, path })
          const files = response.files.map(file => new DBFile(file))
          set({ files })
        } catch (error) {
          console.error('Error fetching files:', error)
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

          if (get().mode === 'authenticated') await useVaultStore.getState().syncVault({ id: get().currVault?.id || 0 })
          await fetchFiles()
        } catch (err) {
          console.error('[FsStore] upload() batch failed:', err)
          set({ uploadError: errorMessage(err, 'Upload failed') })
          throw err
        } finally {
          set({ uploading: false, uploadLabel: null })
        }
      },

      async downloadFile(path) {
        if (get().mode !== 'share') throw new Error('Authenticated downloads are not wired through fsStore')
        if (get().downloading) throw new Error('A download is already running')

        const ws = useShareWebSocketStore.getState()
        await ws.waitForConnection()

        const normalizedPath = normalizeSharePath(path)
        let transferId: string | null = null

        set({ downloading: true, downloadProgress: 0, downloadError: null, downloadLabel: baseName(normalizedPath, 'download') })

        try {
          const startResp = await ws.sendCommand('share.download.start', { path: normalizedPath })
          transferId = startResp.transfer_id
          set({ downloadLabel: startResp.filename })

          const chunkSize = startResp.chunk_size || 256 * 1024
          const chunks: Uint8Array<ArrayBuffer>[] = []
          let offset = 0
          let complete = false

          while (!complete) {
            const chunk = await ws.sendCommand('share.download.chunk', {
              transfer_id: startResp.transfer_id,
              offset,
              length: chunkSize,
            })

            chunks.push(decodeBase64(chunk.data_base64))
            if (!chunk.complete && chunk.next_offset <= offset) throw new Error('Download did not advance')
            offset = chunk.next_offset
            complete = chunk.complete
            const progress = startResp.size_bytes > 0 ? Math.min(100, (offset / startResp.size_bytes) * 100) : complete ? 100 : 0
            set({ downloadProgress: progress })
          }

          saveBrowserDownload(startResp.filename, chunks, startResp.mime_type)
          set({ downloading: false, downloadProgress: 100 })
        } catch (error) {
          if (transferId) await ws.sendCommand('share.download.cancel', { transfer_id: transferId }).catch(() => undefined)
          const message = errorMessage(error, 'Download failed')
          set({ downloading: false, downloadProgress: 0, downloadError: message })
          throw error
        }
      },

      async uploadFile({ file, targetPath = get().path, onProgress }) {
        if (get().mode === 'share') {
          const ws = useShareWebSocketStore.getState()
          await ws.waitForConnection()

          const target = shareUploadTarget(targetPath, get().path, file.name)
          const startResp = await ws.sendCommand('share.upload.start', {
            path: target.path,
            filename: target.filename,
            size_bytes: file.size,
            mime_type: file.type || null,
            duplicate_policy: 'reject',
          })

          const wsInstance = ws.socket
          if (!wsInstance || wsInstance.readyState !== WebSocket.OPEN) throw new Error('Share WebSocket is not connected')
          const chunkSize = startResp.chunk_size || 256 * 1024
          let offset = 0

          try {
            while (offset < file.size) {
              const slice = file.slice(offset, Math.min(offset + chunkSize, file.size))
              const arrayBuffer = await slice.arrayBuffer()
              wsInstance.send(arrayBuffer)

              offset += slice.size
              onProgress?.(slice.size)
            }

            await ws.sendCommand('share.upload.finish', { upload_id: startResp.upload_id })
          } catch (error) {
            await ws.sendCommand('share.upload.cancel', { upload_id: startResp.upload_id }).catch(() => undefined)
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
        set({ currVault: vault })
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
          const response = await ws.sendCommand('share.fs.list', { path: normalizeSharePath(path) })
          return response.entries.map(shareEntryToFsEntry)
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
      onRehydrateStorage: state => {
        if (isShareRoute()) return
        console.log('[FsStore] Rehydrated from storage')
        ;(async () => {
          try {
            await useWebSocketStore.getState().waitForConnection()

            const vaultStore = useVaultStore.getState()
            if (!state.currVault) {
              const localVault = await vaultStore.getLocalVault()
              if (localVault) state.setCurrVault(localVault)
              else console.warn('[FsStore] No local vault found during rehydration')
            }

            if (!state.files || state.files.length === 0) await state.fetchFiles()
          } catch (err) {
            console.error('[FsStore] Rehydrate fetch failed:', err)
          }
        })()
      },
    },
  ),
)
