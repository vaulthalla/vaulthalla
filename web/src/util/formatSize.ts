import type { File as FileModel } from '@/models/file'
import { Directory } from '@/models/directory'

export const formatSize = (item: FileModel | Directory): string => {
  const bytes = item.size_bytes
  if (!bytes) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const val = bytes / 1024 ** i
  return `${val.toFixed(val < 10 ? 1 : 0)} ${units[i]}`
}
