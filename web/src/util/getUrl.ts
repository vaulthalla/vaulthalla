export const getWebsocketUrl = (path = '/ws') => {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:'
  const configuredOrigin = process.env.NEXT_PUBLIC_VAULTHALLA_WS_ORIGIN?.replace(/\/+$/, '')
  const origin = configuredOrigin || `${scheme}//${location.host}`
  const normalizedPath = path.startsWith('/') ? path : `/${path}`
  return `${origin}${normalizedPath}`
}

export const getPreviewUrl = () => '/preview'
