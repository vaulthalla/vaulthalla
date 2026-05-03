type DownloadMode = 'authenticated' | 'share'

interface DownloadUrlOptions {
  mode: DownloadMode
  path?: string | null
  vaultId?: number | null
}

export const normalizeShareDownloadPath = (value?: string | null) => {
  if (!value || value === '.') return '/'
  const raw = value.startsWith('/') ? value : `/${value}`
  const parts = raw.split('/').filter(Boolean)
  if (parts.some(part => part === '.' || part === '..')) throw new Error('Invalid share path')
  return parts.length ? `/${parts.join('/')}` : '/'
}

export const buildDownloadUrl = ({ mode, path, vaultId }: DownloadUrlOptions): string | null => {
  const params = new URLSearchParams()

  if (mode === 'share') {
    params.set('share', '1')
    params.set('path', normalizeShareDownloadPath(path))
  } else {
    if (!vaultId) return null
    params.set('vault_id', String(vaultId))
    params.set('path', path || '/')
  }

  return `/download?${params.toString()}`
}
