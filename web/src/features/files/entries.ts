import { parseDate } from '@/lib/format'

// One normalized filesystem entry for both the authenticated browser and public shares.
export interface Entry {
  key: string
  id: number
  name: string
  path: string
  kind: 'file' | 'dir'
  size: number | null
  mime: string | null
  modified: number // epoch ms, 0 when unknown
  fileCount?: number
  dirCount?: number
}

export const normalizePath = (value?: string | null) => {
  if (!value || value === '.') return '/'
  const parts = (value.startsWith('/') ? value : `/${value}`).split('/').filter(Boolean)
  if (parts.some(part => part === '.' || part === '..')) throw new Error('Invalid path')
  return parts.length ? `/${parts.join('/')}` : '/'
}

export const joinPath = (base: string, name: string) => normalizePath(`${normalizePath(base)}/${name}`)

export const parentOf = (path: string) => {
  const parts = normalizePath(path).split('/').filter(Boolean)
  parts.pop()
  return parts.length ? `/${parts.join('/')}` : '/'
}

export const baseName = (path: string) => normalizePath(path).split('/').filter(Boolean).at(-1) ?? ''

export const pathSegments = (path: string) => normalizePath(path).split('/').filter(Boolean)

const num = (value: unknown): number | null => (typeof value === 'number' && Number.isFinite(value) ? value : null)

type Wire = Record<string, unknown>

export const toEntry = (raw: unknown, parentPath: string): Entry => {
  const wire = (raw ?? {}) as Wire
  const name = typeof wire.name === 'string' ? wire.name : ''
  const path = typeof wire.path === 'string' && wire.path ? normalizePath(wire.path) : joinPath(parentPath, name)
  const isDir = wire.type === 'directory' || 'file_count' in wire || 'subdirectory_count' in wire
  const modified =
    parseDate(wire.updated_at ?? wire.last_modified ?? wire.modified_at ?? wire.updatedAt ?? wire.created_at)?.getTime() ?? 0
  return {
    key: path,
    id: num(wire.id) ?? 0,
    name: name || baseName(path),
    path,
    kind: isDir ? 'dir' : 'file',
    size: num(wire.size_bytes),
    mime: typeof wire.mime_type === 'string' && wire.mime_type ? wire.mime_type : null,
    modified,
    fileCount: num(wire.file_count) ?? undefined,
    dirCount: num(wire.subdirectory_count) ?? undefined,
  }
}

export const sortEntries = (entries: Entry[], key: 'name' | 'size' | 'modified', dir: 'asc' | 'desc') => {
  const m = dir === 'asc' ? 1 : -1
  const collator = new Intl.Collator(undefined, { numeric: true, sensitivity: 'base' })
  return [...entries].sort((a, b) => {
    if (a.kind !== b.kind) return a.kind === 'dir' ? -1 : 1
    if (key === 'size') return ((a.size ?? -1) - (b.size ?? -1)) * m || collator.compare(a.name, b.name)
    if (key === 'modified') return (a.modified - b.modified) * m || collator.compare(a.name, b.name)
    return collator.compare(a.name, b.name) * m
  })
}

const IMAGE = /^image\//
export const isPreviewable = (entry: Entry) =>
  entry.kind === 'file' && Boolean(entry.mime) && (IMAGE.test(entry.mime as string) || entry.mime === 'application/pdf')

export const isImage = (entry: Entry) => entry.kind === 'file' && Boolean(entry.mime) && IMAGE.test(entry.mime as string)

export type FileCategory = 'dir' | 'image' | 'video' | 'audio' | 'pdf' | 'archive' | 'code' | 'text' | 'other'

const EXT: Record<string, FileCategory> = {
  zip: 'archive', gz: 'archive', tgz: 'archive', tar: 'archive', xz: 'archive', zst: 'archive', '7z': 'archive', rar: 'archive', bz2: 'archive',
  js: 'code', ts: 'code', tsx: 'code', jsx: 'code', py: 'code', rs: 'code', go: 'code', c: 'code', h: 'code', cpp: 'code', hpp: 'code',
  java: 'code', rb: 'code', sh: 'code', json: 'code', yaml: 'code', yml: 'code', toml: 'code', sql: 'code', css: 'code', html: 'code',
  txt: 'text', md: 'text', log: 'text', csv: 'text', ini: 'text', conf: 'text',
  mp4: 'video', mkv: 'video', mov: 'video', webm: 'video', avi: 'video',
  mp3: 'audio', flac: 'audio', wav: 'audio', ogg: 'audio', m4a: 'audio',
  pdf: 'pdf',
  png: 'image', jpg: 'image', jpeg: 'image', gif: 'image', webp: 'image', svg: 'image', avif: 'image', heic: 'image',
}

export const categoryOf = (entry: Entry): FileCategory => {
  if (entry.kind === 'dir') return 'dir'
  const mime = entry.mime ?? ''
  if (mime.startsWith('image/')) return 'image'
  if (mime.startsWith('video/')) return 'video'
  if (mime.startsWith('audio/')) return 'audio'
  if (mime === 'application/pdf') return 'pdf'
  const ext = entry.name.includes('.') ? entry.name.split('.').pop()!.toLowerCase() : ''
  return EXT[ext] ?? (mime.startsWith('text/') ? 'text' : 'other')
}
