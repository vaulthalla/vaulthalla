'use client'

import React, { FormEvent, useEffect, useRef, useState } from 'react'
import { Filesystem } from '@/components/fs/Filesystem'
import { FileDropOverlay } from '@/components/fs/FileDropOverlay'
import { Breadcrumbs } from '@/components/fs/breadcrumbs/Breadcrumbs'
import { DownloadCurrentDirectoryButton } from '@/components/fs/DownloadCurrentDirectoryButton'
import UploadProgress from '@/components/loading/UploadProgress'
import CircleNotch from '@/fa-duotone/circle-notch.svg'
import { useFSStore } from '@/stores/fsStore'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { hasShareOperation } from '@/util/shareOperations'
import type { FileWithRelativePath } from '@/models/systemFile'

const Alert = ({ tone = 'info', children }: { tone?: 'info' | 'error' | 'success'; children: React.ReactNode }) => {
  const styles =
    tone === 'error' ? 'border-red-500/40 bg-red-950/40 text-red-100'
    : tone === 'success' ? 'border-emerald-500/40 bg-emerald-950/30 text-emerald-100'
    : 'border-cyan-500/30 bg-cyan-950/30 text-cyan-100'

  return <div className={`rounded border p-3 text-sm ${styles}`}>{children}</div>
}

const ShareLoadingState = () => (
  <section className="flex min-h-[calc(100vh-2rem)] items-center justify-center">
    <div className="flex flex-col items-center gap-4 text-center">
      <div className="flex h-16 w-16 items-center justify-center rounded-full border border-cyan-300/20 bg-cyan-400/5 text-cyan-300 shadow-[0_0_40px_rgba(34,211,238,0.12)]">
        <CircleNotch className="h-8 w-8 animate-spin fill-current" />
      </div>
      <div>
        <p className="text-base font-medium text-cyan-100">Opening secure share session...</p>
        <p className="mt-1 text-sm text-gray-400">Verifying link access</p>
      </div>
    </div>
  </section>
)

const SharePageClient = ({ token }: { token: string }) => {
  const [email, setEmail] = useState('')
  const [code, setCode] = useState('')
  const uploadInputRef = useRef<HTMLInputElement>(null)
  const status = useVaultShareStore(state => state.status)
  const share = useVaultShareStore(state => state.share)
  const error = useVaultShareStore(state => state.error)
  const openSession = useVaultShareStore(state => state.openSession)
  const startEmailChallenge = useVaultShareStore(state => state.startEmailChallenge)
  const confirmEmailChallenge = useVaultShareStore(state => state.confirmEmailChallenge)
  const clearShareSession = useVaultShareStore(state => state.clearShareSession)
  const challengeId = useVaultShareStore(state => state.challengeId)
  const emailSubmitting = useVaultShareStore(state => state.emailSubmitting)
  const codeSubmitting = useVaultShareStore(state => state.codeSubmitting)
  const challengeStartedAt = useVaultShareStore(state => state.challengeStartedAt)
  const files = useFSStore(state => state.files)
  const fetchFiles = useFSStore(state => state.fetchFiles)
  const uploadFiles = useFSStore(state => state.upload)
  const enterShareMode = useFSStore(state => state.enterShareMode)
  const exitShareMode = useFSStore(state => state.exitShareMode)
  const uploadError = useFSStore(state => state.uploadError)
  const uploadLabel = useFSStore(state => state.uploadLabel)
  const uploading = useFSStore(state => state.uploading)
  const uploadProgress = useFSStore(state => state.uploadProgress)
  const downloading = useFSStore(state => state.downloading)
  const downloadLabel = useFSStore(state => state.downloadLabel)
  const downloadProgress = useFSStore(state => state.downloadProgress)
  const downloadError = useFSStore(state => state.downloadError)
  const mode = useFSStore(state => state.mode)
  const uploadSuccess = useFSStore(state => state.uploadSuccess)

  const canList = hasShareOperation(share?.allowed_ops, 'list')
  const canUpload = hasShareOperation(share?.allowed_ops, 'upload')
  const isFileShare = share?.target_type === 'file'
  const isDirectoryShare = share?.target_type === 'directory'
  const canBrowseShare = canList || isFileShare
  const isUploadOnlyDirectoryShare = canUpload && isDirectoryShare && !canList
  const canShowFileSurface = canBrowseShare || isUploadOnlyDirectoryShare

  useEffect(() => {
    enterShareMode()
    openSession(token).catch(() => undefined)

    return () => {
      clearShareSession()
      exitShareMode()
    }
  }, [clearShareSession, enterShareMode, exitShareMode, openSession, token])

  useEffect(() => {
    const reopenIfStale = () => {
      const shareState = useVaultShareStore.getState()
      if (
        shareState.publicToken !== token ||
        shareState.status === 'idle' ||
        shareState.status === 'opening' ||
        shareState.status === 'ready'
      ) {
        openSession(token).catch(() => undefined)
      }
    }

    const handlePageShow = (event: PageTransitionEvent) => {
      if (event.persisted) reopenIfStale()
    }

    const handleVisibilityChange = () => {
      if (document.visibilityState === 'visible') reopenIfStale()
    }

    window.addEventListener('pageshow', handlePageShow)
    document.addEventListener('visibilitychange', handleVisibilityChange)

    return () => {
      window.removeEventListener('pageshow', handlePageShow)
      document.removeEventListener('visibilitychange', handleVisibilityChange)
    }
  }, [openSession, token])

  useEffect(() => {
    if (mode === 'share' && status === 'ready' && canBrowseShare) fetchFiles().catch(() => undefined)
  }, [canBrowseShare, fetchFiles, mode, status])

  const submitEmail = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    await startEmailChallenge(email).catch(() => undefined)
  }

  const submitCode = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    await confirmEmailChallenge(code).catch(() => undefined)
  }

  const handleUploadInputChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const selected = Array.from(event.target.files ?? [])
    if (!selected.length) return

    const files = selected.map(file => {
      Object.defineProperty(file, 'relativePath', { value: file.name, writable: false })
      return file as FileWithRelativePath
    })

    uploadFiles(files)
      .catch(() => undefined)
      .finally(() => {
        event.target.value = ''
      })
  }

  return (
    <main className="min-h-screen bg-gray-950 px-3 py-3 text-white sm:px-6">
      <div className="mx-auto flex max-w-7xl flex-col gap-3">
        {status === 'opening' || status === 'idle' ? (
          <ShareLoadingState />
        ) : null}

        {status === 'email_required' && (
          <section className="grid gap-4 rounded border border-white/10 bg-gray-900 p-4 md:grid-cols-[minmax(0,0.95fr)_minmax(0,1.05fr)]">
            <div>
              <h2 className="text-xl font-semibold">Email verification required</h2>
              <p className="mt-2 text-sm text-gray-300">
                Enter the email address this share was sent to, then enter the verification code.
              </p>
              {challengeStartedAt && (
                <Alert tone="success">
                  Code requested. Check your email, then enter the code here. You can resend if it expires.
                </Alert>
              )}
              {error && <Alert tone="error">{error}</Alert>}
            </div>

            <div className="space-y-4">
              <form className="flex flex-col gap-3" onSubmit={submitEmail}>
                <label className="flex flex-col gap-1 text-sm">
                  Email
                  <input
                    className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
                    type="email"
                    value={email}
                    onChange={event => setEmail(event.target.value)}
                    required
                    disabled={emailSubmitting}
                  />
                </label>
                <button className="rounded bg-cyan-500 px-3 py-2 font-medium text-gray-950 disabled:opacity-60" type="submit" disabled={emailSubmitting}>
                  {challengeId ? 'Resend Code' : 'Send Code'}
                </button>
              </form>

              <form className="flex flex-col gap-3" onSubmit={submitCode}>
                <label className="flex flex-col gap-1 text-sm">
                  Code
                  <input
                    className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
                    value={code}
                    onChange={event => setCode(event.target.value)}
                    required
                    disabled={!challengeId || codeSubmitting}
                  />
                </label>
                <button
                  className="rounded bg-white px-3 py-2 font-medium text-gray-950 disabled:opacity-60"
                  type="submit"
                  disabled={!challengeId || codeSubmitting}>
                  Verify
                </button>
              </form>
            </div>
          </section>
        )}

        {status === 'ready' && mode !== 'share' && <ShareLoadingState />}

        {status === 'ready' && mode === 'share' && (
          <>
            {!isUploadOnlyDirectoryShare && (
              <div className="flex items-center justify-between gap-3">
                <Breadcrumbs className="min-w-0 flex-1" />
                <DownloadCurrentDirectoryButton />
              </div>
            )}

            {(downloading || downloadError) && (
              <Alert tone={downloadError ? 'error' : 'info'}>
                {downloadError || `Downloading ${downloadLabel || 'file'}... ${Math.round(downloadProgress)}%`}
              </Alert>
            )}

            {(uploading || uploadError) && (
              <Alert tone={uploadError ? 'error' : 'info'}>
                {uploadError || `Uploading ${uploadLabel || 'file'}... ${Math.round(uploadProgress)}%`}
              </Alert>
            )}

            {isUploadOnlyDirectoryShare ? (
              <FileDropOverlay disabled={!canUpload || !isDirectoryShare} disabledMessage="Upload is not available for this share">
                <UploadProgress />
                <section className="rounded border border-dashed border-cyan-500/35 bg-gray-900 p-8 text-center text-gray-200">
                  <h2 className="text-xl font-semibold text-white">Upload files to this share</h2>
                  <p className="mt-2 text-sm text-gray-400">Uploaded files may not be visible after upload.</p>
                  {uploadSuccess && (
                    <div className="mt-5 rounded border border-emerald-500/40 bg-emerald-950/30 p-3 text-sm text-emerald-100">
                      {uploadSuccess}
                    </div>
                  )}
                  <div className="mt-5 flex flex-col items-center gap-3 text-sm text-cyan-200 sm:flex-row sm:justify-center">
                    <input
                      ref={uploadInputRef}
                      className="hidden"
                      type="file"
                      multiple
                      onChange={handleUploadInputChange}
                      disabled={uploading}
                    />
                    <button
                      type="button"
                      className="rounded bg-cyan-400 px-4 py-2 font-medium text-gray-950 transition hover:bg-cyan-300 disabled:cursor-not-allowed disabled:opacity-60"
                      disabled={uploading}
                      onClick={() => uploadInputRef.current?.click()}>
                      Select files
                    </button>
                    <span>or drop files here.</span>
                  </div>
                </section>
              </FileDropOverlay>
            ) : canShowFileSurface ? (
              <FileDropOverlay disabled={!canUpload || !isDirectoryShare} disabledMessage="Upload is not available for this share">
                <UploadProgress />
                {files.length === 0 ? (
                  <section className="rounded border border-dashed border-gray-700 bg-gray-900 p-8 text-center text-gray-300">
                    This directory is empty.
                    {canUpload && isDirectoryShare && <div className="mt-2 text-sm text-cyan-200">Drop files here to add them.</div>}
                  </section>
                ) : (
                  <Filesystem files={files} />
                )}
              </FileDropOverlay>
            ) : (
              <Alert tone="info">
                Directory listing is not available for this share. Use a link with list access to browse contents.
              </Alert>
            )}
          </>
        )}

        {(status === 'error' || status === 'expired' || status === 'revoked') && (
          <section className="rounded border border-red-500/40 bg-red-950/40 p-6 text-red-100">
            <h2 className="text-xl font-semibold">Share unavailable</h2>
            <p className="mt-2 text-sm">{error || 'This share could not be opened. It may be expired, revoked, or disabled.'}</p>
            <button className="mt-4 rounded border border-red-300/50 px-3 py-2 text-sm hover:bg-red-900/40" onClick={() => openSession(token).catch(() => undefined)}>
              Retry
            </button>
          </section>
        )}
      </div>
    </main>
  )
}

export default SharePageClient
