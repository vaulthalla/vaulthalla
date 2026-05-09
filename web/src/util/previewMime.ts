export const normalizedPreviewMime = (mime?: string | null) => (
  mime?.split(';', 1)[0]?.trim().toLowerCase() || ''
)

export const isSvgPreviewMime = (mime?: string | null) => normalizedPreviewMime(mime) === 'image/svg+xml'

export const isPreviewableMime = (mime?: string | null) => {
  const normalized = normalizedPreviewMime(mime)
  return normalized.startsWith('image/') || normalized === 'application/pdf'
}
