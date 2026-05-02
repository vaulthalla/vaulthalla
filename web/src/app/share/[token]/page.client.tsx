'use client'

import React, { FormEvent, useEffect, useState } from 'react'
import { Filesystem } from '@/components/fs/Filesystem'
import { FileDropOverlay } from '@/components/fs/FileDropOverlay'
import { Breadcrumbs } from '@/components/fs/breadcrumbs/Breadcrumbs'
import UploadProgress from '@/components/loading/UploadProgress'
import { useFSStore } from '@/stores/fsStore'
import { useVaultShareStore } from '@/stores/vaultShareStore'
import { formatShareDate, hasShareOperation } from '@/util/shareOperations'
import DownloadIcon from '@/fa-duotone/download.svg'

const Alert = ({ tone = 'info', children }: { tone?: 'info' | 'error' | 'success'; children: React.ReactNode }) => {
  const styles =
    tone === 'error' ? 'border-red-500/40 bg-red-950/40 text-red-100'
    : tone === 'success' ? 'border-emerald-500/40 bg-emerald-950/30 text-emerald-100'
    : 'border-cyan-500/30 bg-cyan-950/30 text-cyan-100'

  return <div className={`rounded border p-3 text-sm ${styles}`}>{children}</div>
}

const SharePageClient = ({ token }: { token: string }) => {
  const [email, setEmail] = useState('')
  const [code, setCode] = useState('')
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

  const canList = hasShareOperation(share?.allowed_ops, 'list')
  const canDownload = hasShareOperation(share?.allowed_ops, 'download')
  const canUpload = hasShareOperation(share?.allowed_ops, 'upload')
  const isFileShare = share?.target_type === 'file'
  const isDirectoryShare = share?.target_type === 'directory'
  const canShowFileSurface = canList || isFileShare || (canUpload && isDirectoryShare)
  const title = share?.public_label || share?.metadata?.label?.toString() || 'Shared files'
  const expiresAt = share?.expires_at ? formatShareDate(share.expires_at) : null

  useEffect(() => {
    enterShareMode()
    openSession(token).catch(() => undefined)

    return () => {
      clearShareSession()
      exitShareMode()
    }
  }, [clearShareSession, enterShareMode, exitShareMode, openSession, token])

  useEffect(() => {
    if (status === 'ready' && (canList || isFileShare)) fetchFiles().catch(() => undefined)
  }, [canList, fetchFiles, isFileShare, status])

  const submitEmail = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    await startEmailChallenge(email).catch(() => undefined)
  }

  const submitCode = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    await confirmEmailChallenge(code).catch(() => undefined)
  }

  return (
    <main className="min-h-screen bg-gray-950 px-3 py-4 text-white sm:px-6">
      <div className="mx-auto flex max-w-7xl flex-col gap-4">
        <header className="rounded-lg border border-white/10 bg-gray-900/90 p-3 shadow-lg">
          <div className="min-w-0">
            <p className="text-xs font-medium uppercase tracking-wide text-cyan-300">Vaulthalla Share</p>
            <h1 className="truncate text-xl font-semibold">{title}</h1>
            <div className="mt-1 flex flex-wrap items-center gap-x-3 gap-y-1 text-xs text-gray-400">
              {share?.root_path && <span className="truncate">{share.root_path}</span>}
              {expiresAt && <span>Expires {expiresAt}</span>}
            </div>
          </div>
        </header>

        {status === 'opening' || status === 'idle' ? (
          <section className="rounded border border-white/10 bg-gray-900 p-6">
            <div className="h-2 w-full overflow-hidden rounded bg-gray-800">
              <div className="h-full w-1/2 animate-pulse rounded bg-cyan-400" />
            </div>
            <p className="mt-4 text-gray-300">Opening secure share session...</p>
          </section>
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

        {status === 'ready' && (
          <>
            <div className="flex items-center justify-between gap-3">
              <Breadcrumbs className="min-w-0 flex-1" />
              {canDownload && isDirectoryShare && (
                <button
                  className="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg border border-white/10 text-cyan-200 opacity-45"
                  type="button"
                  title="Directory archive download is not available yet"
                  aria-label="Directory archive download is not available yet"
                  disabled>
                  <DownloadIcon className="h-4 w-4 fill-current" />
                </button>
              )}
            </div>

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

            {canShowFileSurface ? (
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
