import React from 'react'
import { cn } from '@/util/cn'
import { categoryOf, type Entry, type FileCategory } from '@/features/files/entries'
import {
  FileAudioIcon,
  FileCodeIcon,
  FileIcon as FileGenericIcon,
  FileImageIcon,
  FileLinesIcon,
  FilePdfIcon,
  FileVideoIcon,
  FileZipperIcon,
  FolderIcon,
} from '@/components/ui/icons'

const ICONS: Record<FileCategory, React.ComponentType<React.SVGProps<SVGSVGElement>>> = {
  dir: FolderIcon,
  image: FileImageIcon,
  video: FileVideoIcon,
  audio: FileAudioIcon,
  pdf: FilePdfIcon,
  archive: FileZipperIcon,
  code: FileCodeIcon,
  text: FileLinesIcon,
  other: FileGenericIcon,
}

const TINTS: Record<FileCategory, string> = {
  dir: 'text-accent-text',
  image: 'text-info',
  video: 'text-violet',
  audio: 'text-pink',
  pdf: 'text-danger',
  archive: 'text-warn',
  code: 'text-ok',
  text: 'text-fg-muted',
  other: 'text-fg-subtle',
}

export const FileIcon = ({ entry, className }: { entry: Entry; className?: string }) => {
  const category = categoryOf(entry)
  const Icon = ICONS[category]
  return <Icon aria-hidden className={cn('size-[18px] shrink-0', TINTS[category], className)} />
}
