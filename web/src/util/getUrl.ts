export const getWebsocketUrl = (path = '/ws') => {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${scheme}//${location.host}${path}`
}

export const getPreviewUrl = () => '/preview'
