'use client'

import Link from 'next/link'
import AlertTriangleIcon from '@/fa-regular/triangle-exclamation.svg'
import { useFSStore } from '@/stores/fsStore'

export const UploadErrorBanner = () => {
  const uploadError = useFSStore(state => state.uploadError)
  const uploadErrorVaultId = useFSStore(state => state.uploadErrorVaultId)

  if (!uploadError) return null

  return (
    <div
      role="alert"
      aria-live="assertive"
      className="mb-3 flex flex-col gap-3 rounded-lg border border-red-400/60 bg-red-950/85 px-4 py-3 text-red-50 shadow-lg shadow-red-950/40 sm:flex-row sm:items-center sm:justify-between">
      <div className="flex min-w-0 items-start gap-3">
        <AlertTriangleIcon className="mt-0.5 h-5 w-5 shrink-0 fill-current text-red-200" />
        <div className="min-w-0">
          <div className="text-sm font-semibold">Upload blocked</div>
          <div className="mt-0.5 text-sm text-red-100">{uploadError}</div>
        </div>
      </div>
      {uploadErrorVaultId && (
        <Link
          href={`/vaults/${uploadErrorVaultId}/edit`}
          className="inline-flex shrink-0 items-center justify-center rounded-md border border-red-200/50 bg-red-100 px-3 py-1.5 text-sm font-semibold text-red-950 transition hover:bg-white">
          Edit vault quota
        </Link>
      )}
    </div>
  )
}

export default UploadErrorBanner
