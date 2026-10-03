import type { ShareLink, ShareLinkType, ShareOperation } from '@/models/linkShare'
import type { Tone } from '@/lib/tone'
import { shareOperations } from '@/util/shareOperations'

export type SharePreset = 'access' | 'download' | 'upload'

export const PRESETS: Record<SharePreset, { title: string; description: string; role: string; linkType: ShareLinkType; dirOnly?: boolean }> = {
  access: {
    title: 'Browse & download',
    description: 'Recipients can open folders, preview and download.',
    role: 'share_browse_download',
    linkType: 'access',
  },
  download: {
    title: 'Download only',
    description: 'A direct download handoff with no browsing.',
    role: 'share_download_only',
    linkType: 'download',
  },
  upload: {
    title: 'Upload dropbox',
    description: 'Recipients can drop files in. They can’t see what’s already there.',
    role: 'share_upload_dropbox',
    linkType: 'upload',
    dirOnly: true,
  },
}

export const presetOperations = (preset: SharePreset, isDirectory: boolean): ShareOperation[] => {
  if (preset === 'upload') return ['upload']
  if (preset === 'download') return isDirectory ? ['metadata', 'list', 'download'] : ['metadata', 'download']
  return isDirectory ? ['metadata', 'list', 'preview', 'download'] : ['metadata', 'preview', 'download']
}

export type ShareState = 'active' | 'expired' | 'revoked' | 'disabled'

export const shareState = (share: Pick<ShareLink, 'revoked_at' | 'disabled_at' | 'expires_at'>): ShareState => {
  if (share.revoked_at) return 'revoked'
  if (share.disabled_at) return 'disabled'
  if (share.expires_at && Date.parse(share.expires_at) < Date.now()) return 'expired'
  return 'active'
}

export const shareStateTone: Record<ShareState, Tone> = { active: 'ok', expired: 'warn', disabled: 'warn', revoked: 'neutral' }

const OP_LABEL: Record<ShareOperation, string> = {
  metadata: 'details',
  list: 'browse',
  preview: 'preview',
  download: 'download',
  upload: 'upload',
  mkdir: 'create folders',
  overwrite: 'overwrite',
}

export const describeOps = (ops: ShareLink['allowed_ops']) => {
  const list = shareOperations(ops).filter(op => op !== 'metadata')
  return list.length ? list.map(op => OP_LABEL[op]).join(', ') : 'nothing'
}

export const publicUrl = (path: string) => (typeof window === 'undefined' ? path : new URL(path, window.location.origin).toString())
