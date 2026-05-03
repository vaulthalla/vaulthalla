import React from 'react'
import type { File as FileModel } from '@/models/file'
import type { Directory } from '@/models/directory'
import { buildPreviewUrl } from '@/util/previewUrl'
import { formatSize } from '@/util/formatSize'
import { formatTimestamp } from '@/util/formatTimestamp'
import type { FileSystemProps, FilesystemEntry, FilesystemRow } from '@/components/fs/types'
import { FilesystemClient } from './Filesystem.client'

const isDirectory = (file: FileModel | Directory): file is Directory => (file as Directory).file_count !== undefined

function buildRows(files: FilesystemEntry[], mode: 'authenticated' | 'share'): FilesystemRow[] {
  return files.map(f => {
    const key = `${f.vault_id}:${f.path ?? f.name}`

    if (isDirectory(f)) {
      return {
        ...f,
        key,
        entryType: 'directory',
        size: formatSize(f),
        modified: formatTimestamp(f.updated_at),
        previewUrl: null,
      }
    }

    const explicitPreviewUrl = (f as FileModel & { previewUrl?: string | null }).previewUrl

    return {
      ...f,
      key,
      entryType: 'file',
      size: formatSize(f),
      modified: formatTimestamp(f.updated_at),
      previewUrl: explicitPreviewUrl || (mode === 'authenticated' ?
        buildPreviewUrl({ mode: 'authenticated', vaultId: f.vault_id, path: f.path || f.name, size: 64 })
      : null),
    }
  })
}

export const Filesystem = ({ files, previewMode = 'authenticated' }: FileSystemProps) => {
  const rows = React.useMemo(() => buildRows(files, previewMode), [files, previewMode])
  return <FilesystemClient rows={rows} />
}

export default Filesystem
