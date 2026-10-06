// Vault/share path helpers. Kept separate from entries.ts: the transfer manager in the console shell needs only
// these, and anything it imports lands in every route's first-load JavaScript.

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
