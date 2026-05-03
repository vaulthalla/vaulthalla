import type React from 'react'
import type { File as FileModel } from '@/models/file'
import type { Directory } from '@/models/directory'

export type FilesystemEntry = FileModel | Directory

export interface FileSystemProps {
  files: FilesystemEntry[]
  previewMode?: 'authenticated' | 'share'
}

interface FormattedRowFields {
  key: string
  size: string
  modified: string
  previewUrl: string | null
}

export type FileRow = FileModel & FormattedRowFields & { entryType: 'file' }

export type DirectoryRow = Directory & FormattedRowFields & { entryType: 'directory'; previewUrl: null }

export type FilesystemRow = FileRow | DirectoryRow

export interface RowProps {
  r: FilesystemRow
  onNavigate: (path: string) => void
  onOpenFile: (f: FileModel) => void
  onContextMenu: (e: React.MouseEvent, r: FilesystemRow) => void
}
