import React from 'react'
import type { File as FileModel } from '@/models/file'
import type { Directory } from '@/models/directory'
import { getPreviewUrl } from '@/util/getUrl'
import { formatSize } from '@/util/formatSize'
import type { FileSystemProps, FilesystemEntry, FilesystemRow } from '@/components/fs/types'
import { FilesystemClient } from './Filesystem.client'

const isDirectory = (file: FileModel | Directory): file is Directory => (file as Directory).file_count !== undefined

function buildRows(files: FilesystemEntry[]): FilesystemRow[] {
  return files.map(f => {
    const key = `${f.vault_id}:${f.path ?? f.name}`

    if (isDirectory(f)) {
      return {
        ...f,
        key,
        entryType: 'directory',
        size: formatSize(f),
        modified: new Date(f.updated_at).toLocaleString(),
        previewUrl: null,
      }
    }

    return {
      ...f,
      key,
      entryType: 'file',
      size: formatSize(f),
      modified: new Date(f.updated_at).toLocaleString(),
      previewUrl: `${getPreviewUrl()}?vault_id=${f.vault_id}&path=${encodeURIComponent(f.path || f.name)}&size=64`,
    }
  })
}

export const Filesystem = ({ files }: FileSystemProps) => {
  const rows = buildRows(files)
  return <FilesystemClient rows={rows} />
}

export default Filesystem
