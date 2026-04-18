import { create } from 'zustand'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { WSCommandPayload } from '@/util/webSocketCommands'
import { File as DBFile } from '@/models/file'
import { LocalDiskVault, S3Vault, Vault } from '@/models/vaults'
import { persist } from 'zustand/middleware'
import { useVaultStore } from '@/stores/vaultStore'
import { FileWithRelativePath } from '@/models/systemFile'
import { Directory } from '@/models/directory'

interface FsStore {
  currVault: Vault | LocalDiskVault | S3Vault | null
  path: string
  uploading: boolean
  uploadProgress: number
  files: DBFile[]
  copiedItem: DBFile | Directory | null
  setCopiedItem: (item: DBFile | Directory | null) => void
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
  delete: (name: string) => Promise<void>
  mkdir: (payload: WSCommandPayload<'fs.dir.create'>) => Promise<void>
  move: (payload: WSCommandPayload<'fs.entry.move'>) => Promise<void>
  copy: (payload: WSCommandPayload<'fs.entry.copy'>) => Promise<void>
  rename: (payload: WSCommandPayload<'fs.entry.rename'>) => Promise<void>
  setCurrVault: (vault: Vault) => void
  setPath: (dir: string) => void
  listDirectory: (payload: WSCommandPayload<'fs.dir.list'>) => Promise<DBFile[]>
}

export const useFSStore = create<FsStore>()(
  persist(
    (set, get) => ({
      currVault: null,
      path: '',
      uploading: false,
      uploadProgress: 0,
      files: [],
      copiedItem: null,

      setCopiedItem(item) {
        set({ copiedItem: item })
      },

      async pasteCopiedItem(targetPath) {
        const { copiedItem, currVault, path } = get()
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
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        const { currVault } = get()
        if (!currVault) {
          console.warn('[FsStore] No current vault to set')
          return
        }

        try {
          const response = await ws.sendCommand('fs.dir.list', { vault_id: currVault.id, path: get().path })

          console.log(response.files)
          const files = response.files.map(file => new DBFile(file))
          set({ files })
        } catch (error) {
          console.error('Error fetching files:', error)
          throw error
        }
      },

      async upload(files: FileWithRelativePath[]) {
        const { uploadFile, fetchFiles, path } = get()

        set({ uploading: true, uploadProgress: 0 })

        const totalBytes = files.reduce((sum, f) => sum + f.size, 0)
        let uploadedBytes = 0

        try {
          for (const file of files) {
            await uploadFile({
              file,
              targetPath: path + '/' + file.relativePath,
              onProgress: bytes => {
                uploadedBytes += bytes
                set({ uploadProgress: Math.min(100, (uploadedBytes / totalBytes) * 100) })
              },
            })
          }

          await useVaultStore.getState().syncVault({ id: get().currVault?.id || 0 })
          await fetchFiles()
        } catch (err) {
          console.error('[FsStore] upload() batch failed:', err)
          throw err
        } finally {
          set({ uploading: false })
        }
      },

      async uploadFile({ file, targetPath = get().path, onProgress }) {
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
            const slice = file.slice(offset, offset + chunkSize)
            const arrayBuffer = await slice.arrayBuffer()
            wsInstance.send(arrayBuffer)

            offset += chunkSize
            onProgress?.(chunkSize)
          }

          await ws.sendCommand('fs.upload.finish', { vault_id: currVault.id, path: targetPath })
        } catch (err) {
          console.error('[FsStore] uploadFile error:', err)
          throw err
        }
      },

      async delete(name) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        const { currVault } = get()
        if (!currVault) {
          console.warn('[FsStore] No current vault set for deletion')
          return
        }

        const path = get().path + '/' + name

        try {
          await ws.sendCommand('fs.entry.delete', { vault_id: currVault.id, path })
          console.log('[FsStore] deleted:', path)
          await get().fetchFiles()
        } catch (error) {
          console.error('Error deleting:', error)
          throw error
        }
      },

      async mkdir({ vault_id, path }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          const response = await ws.sendCommand('fs.dir.create', { vault_id, path })
          console.log('[FsStore] Directory created:', response.path)
          await get().fetchFiles()
        } catch (error) {
          console.error('Error creating directory:', error)
          throw error
        }
      },

      async move({ vault_id, from, to }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.entry.move', { vault_id, from, to })
          console.log('[FsStore] Moved:', from, 'to', to)
          await get().fetchFiles()
        } catch (error) {
          console.error('Error moving file:', error)
          throw error
        }
      },

      async copy({ vault_id, from, to }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.entry.copy', { vault_id, from, to })
          console.log('[FsStore] Copied:', from, 'to', to)
          await get().fetchFiles()
        } catch (error) {
          console.error('Error copying file:', error)
          throw error
        }
      },

      async rename({ vault_id, from, to }) {
        const ws = useWebSocketStore.getState()
        await ws.waitForConnection()

        try {
          await ws.sendCommand('fs.entry.rename', { vault_id, from, to })
          console.log('[FsStore] Renamed:', from, 'to', to)
          await get().fetchFiles()
        } catch (error) {
          console.error('Error renaming file:', error)
          throw error
        }
      },

      async setCurrVault(vault) {
        set({ currVault: vault })
        await get().fetchFiles()
      },

      async setPath(dir) {
        set({ path: dir })
        await get().fetchFiles()
      },

      async listDirectory({ vault_id, path = get().path }) {
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
      partialize: state => ({ currVault: state.currVault, path: state.path }),
      onRehydrateStorage: state => {
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
