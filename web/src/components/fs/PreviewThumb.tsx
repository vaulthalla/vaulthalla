'use client'

import React from 'react'
import FileIcon from '@/fa-duotone/file.svg'
import Thumb from '@/components/fs/Thumb'
import { usePreviewCacheStore } from '@/components/fs/previewCacheStore'

interface PreviewThumbProps {
  cacheKey: string
  signature: string
  directSrc: string
  alt: string
}

const unavailableStatuses = new Set(['missing', 'unsupported', 'error'])

const PreviewThumb = React.memo(function PreviewThumb({ cacheKey, signature, directSrc, alt }: PreviewThumbProps) {
  const cacheEntry = usePreviewCacheStore(state => state.entries[cacheKey])
  const entry = cacheEntry?.signature === signature ? cacheEntry : undefined
  const isConfirmedUnavailable = Boolean(
    entry && (unavailableStatuses.has(entry.status) || (entry.status === 'ready' && !entry.url)),
  )

  if (isConfirmedUnavailable) return <FileIcon className="text-primary fill-current" />

  const src = entry?.status === 'ready' && entry.url ? entry.url : directSrc
  const thumbKey = `${signature}:${entry?.status ?? 'direct'}:${src}`
  return <Thumb key={thumbKey} src={src} alt={alt} />
})

export default PreviewThumb
