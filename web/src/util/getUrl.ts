export const getWebsocketUrl = () => {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${scheme}//${location.host}/ws`
}

export const getPreviewUrl = () => '/preview'
