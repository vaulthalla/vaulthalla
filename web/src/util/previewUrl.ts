import { getPreviewUrl } from '@/util/getUrl'

type PreviewMode = 'authenticated' | 'share'

interface PreviewUrlOptions {
  mode: PreviewMode
  path?: string | null
  vaultId?: number | null
  size?: number
  scale?: number
}

export const normalizeSharePreviewPath = (value?: string | null) => {
  if (!value || value === '.') return '/'
  const raw = value.startsWith('/') ? value : `/${value}`
  const parts = raw.split('/').filter(Boolean)
  if (parts.some(part => part === '.' || part === '..')) throw new Error('Invalid share path')
  return parts.length ? `/${parts.join('/')}` : '/'
}

export const buildPreviewUrl = ({ mode, path, vaultId, size, scale }: PreviewUrlOptions): string | null => {
  const params = new URLSearchParams()

  if (mode === 'share') {
    params.set('share', '1')
    params.set('path', normalizeSharePreviewPath(path))
  } else {
    if (!vaultId) return null
    params.set('vault_id', String(vaultId))
    params.set('path', path || '/')
  }

  if (size && size > 0) params.set('size', String(size))
  if (scale && scale > 0) params.set('scale', String(scale))

  return `${getPreviewUrl()}?${params.toString()}`
}
