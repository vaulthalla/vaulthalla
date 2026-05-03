import { ShareLink, ShareOperation, ShareStatus } from '@/models/linkShare'

type ShareOperationSource = {
  allowed_ops?: ShareLink['allowed_ops']
  effective_allowed_ops?: ShareLink['allowed_ops']
}

export const shareOpBits: Record<ShareOperation, number> = {
  metadata: 1 << 0,
  list: 1 << 1,
  preview: 1 << 2,
  download: 1 << 3,
  upload: 1 << 4,
  mkdir: 1 << 5,
  overwrite: 1 << 6,
}

export const shareOperations = (allowedOps: ShareLink['allowed_ops']): ShareOperation[] => {
  if (Array.isArray(allowedOps)) return allowedOps
  return Object.entries(shareOpBits)
    .filter(([, bit]) => (allowedOps & bit) !== 0)
    .map(([op]) => op as ShareOperation)
}

export const hasShareOperation = (allowedOps: ShareLink['allowed_ops'] | undefined, operation: ShareOperation) => {
  if (!allowedOps) return false
  return shareOperations(allowedOps).includes(operation)
}

export const shareDecisionOps = (share: ShareOperationSource | null | undefined) => (
  share?.effective_allowed_ops ?? share?.allowed_ops
)

export const hasEffectiveShareOperation = (
  share: ShareOperationSource | null | undefined,
  operation: ShareOperation,
) => hasShareOperation(shareDecisionOps(share), operation)

export const canRequestSharePreview = (share: ShareOperationSource | null | undefined) => {
  return hasEffectiveShareOperation(share, 'preview')
}

export const shareOperationLabel = (allowedOps: ShareLink['allowed_ops'] | undefined) => {
  if (!allowedOps) return 'No operations'
  const ops = shareOperations(allowedOps)
  return ops.length ? ops.join(', ') : 'No operations'
}

export const statusForShare = (share: Pick<ShareLink, 'revoked_at' | 'disabled_at' | 'expires_at'>): ShareStatus => {
  if (share.revoked_at) return 'revoked'
  if (share.disabled_at) return 'error'
  if (share.expires_at && Date.parse(share.expires_at) < Date.now()) return 'expired'
  return 'ready'
}

export const managementStatusLabel = (share: Pick<ShareLink, 'revoked_at' | 'disabled_at' | 'expires_at'>) => {
  if (share.revoked_at) return 'revoked'
  if (share.disabled_at) return 'disabled'
  if (share.expires_at && Date.parse(share.expires_at) < Date.now()) return 'expired'
  return 'active'
}

export const shareUrl = (publicUrlPath: string) => {
  if (typeof window === 'undefined') return publicUrlPath
  return new URL(publicUrlPath, window.location.origin).toString()
}

export const formatShareDate = (value?: string | null) => {
  if (!value) return 'No expiration'
  const parsed = Date.parse(value)
  return Number.isNaN(parsed) ? value : new Date(parsed).toLocaleString()
}
