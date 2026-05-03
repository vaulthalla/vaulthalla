'use client'

import React from 'react'
import * as motion from 'motion/react-client'
import { AnimatePresence } from 'motion/react'
import Image from 'next/image'
import type { File as FileModel } from '@/models/file'
import type { SharePreviewResponse } from '@/models/linkShare'
import { buildPreviewUrl } from '@/util/previewUrl'
import { formatTimestamp } from '@/util/formatTimestamp'
import { useFSStore } from '@/stores/fsStore'
import X from '@/fa-duotone/x.svg'

interface FilePreviewModalProps {
  file: FileModel | null
  sharePreview?: SharePreviewResponse | null
  onClose: () => void
}

export const FilePreviewModal: React.FC<FilePreviewModalProps> = ({ file, sharePreview, onClose }) => {
  const mode = useFSStore(state => state.mode)

  if (!file && !sharePreview) return null

  const filePath = file?.path || file?.name || '/'
  const previewUrl = file ? buildPreviewUrl({
    mode: mode === 'share' ? 'share' : 'authenticated',
    vaultId: file.vault_id,
    path: filePath,
    size: 1024,
  }) || '' : ''
  const sharePreviewUrl = sharePreview ? `data:${sharePreview.mime_type};base64,${sharePreview.data_base64}` : ''
  const sourceUrl = sharePreview ? sharePreviewUrl : previewUrl
  const sourceMime = sharePreview?.source_mime_type || file?.mime_type || ''
  const previewMime = sharePreview?.mime_type || sourceMime
  const httpPreviewRendersImage = !sharePreview && (sourceMime.startsWith('image/') || sourceMime === 'application/pdf')
  const isPdf = !httpPreviewRendersImage && previewMime === 'application/pdf'
  const isImage = previewMime.startsWith('image/') || httpPreviewRendersImage
  const canPreview = Boolean(sourceUrl && (isPdf || isImage))

  const meta = [
    { label: 'Name', value: sharePreview?.filename || file?.name || 'Preview' },
    { label: 'Size', value: formatSize(sharePreview?.size_bytes ?? file?.size_bytes ?? 0) },
    { label: 'MIME', value: sharePreview?.source_mime_type || file?.mime_type || 'unknown' },
    ...(file ? [{ label: 'Last Modified', value: formatTimestamp(file.updated_at) }] : []),
  ]

  return (
    <AnimatePresence>
      <motion.div
        key="modal"
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        exit={{ opacity: 0 }}
        className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 backdrop-blur-sm"
        onClick={onClose}>
        <motion.div
          initial={{ scale: 0.95, opacity: 0 }}
          animate={{ scale: 1, opacity: 1 }}
          exit={{ scale: 0.95, opacity: 0 }}
          transition={{ type: 'spring', stiffness: 220, damping: 20 }}
          className="relative z-10 mx-4 w-full max-w-4xl overflow-hidden rounded-2xl border border-gray-700 bg-gray-900/95 shadow-2xl"
          onClick={e => e.stopPropagation()}>
          <button
            onClick={onClose}
            className="absolute top-4 right-4 z-20 rounded-full bg-gray-800/80 p-2 transition hover:bg-gray-700">
            <X className="h-5 w-5 text-white" />
          </button>

          <div className="grid grid-cols-1 gap-0 md:grid-cols-3">
            <div className="relative col-span-2 flex aspect-video h-75 items-center justify-center bg-gray-800 md:aspect-auto md:h-[500px]">
              {canPreview && sourceUrl && isPdf ?
                <object
                  className="h-full w-full rounded-l-2xl bg-white"
                  data={sourceUrl}
                  type="application/pdf"
                  aria-label={sharePreview?.filename || file?.name || 'Preview'}
                />
              : canPreview && sourceUrl && isImage ?
                <Image
                  src={sourceUrl}
                  alt={sharePreview?.filename || file?.name || 'Preview'}
                  fill
                  className="rounded-l-2xl object-contain"
                  sizes="(max-width: 768px) 100vw, 66vw"
                  priority
                  unoptimized
                />
              : <div className="px-6 text-center text-sm text-gray-300">Preview is unavailable for this file.</div>}
            </div>
            <div className="flex flex-col justify-center space-y-4 p-6 text-white">
              <h2 className="text-xl font-semibold">{sharePreview?.filename || file?.name || 'Preview'}</h2>
              <div className="space-y-2 text-sm">
                {meta.map(m => (
                  <div key={m.label} className="flex justify-between border-b border-gray-700 pb-1">
                    <span className="text-gray-400">{m.label}</span>
                    <span>{m.value}</span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </motion.div>
      </motion.div>
    </AnimatePresence>
  )
}

const formatSize = (bytes: number): string => {
  if (!bytes) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / Math.pow(1024, i)
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[i]}`
}
