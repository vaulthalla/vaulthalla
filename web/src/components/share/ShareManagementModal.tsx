'use client'

import React, { FormEvent, useEffect, useMemo, useState } from 'react'
import { createPortal } from 'react-dom'
import X from '@/fa-duotone/x.svg'
import CopyIcon from '@/fa-duotone/copy.svg'
import RotateIcon from '@/fa-duotone/rotate-right.svg'
import BanIcon from '@/fa-duotone/ban.svg'
import { FilesystemRow } from '@/components/fs/types'
import { ShareLinkType, ShareAccessMode, ShareOperation } from '@/models/linkShare'
import { useShareManagementStore } from '@/stores/shareManagementStore'
import { Vault } from '@/models/vaults'
import { managementStatusLabel, shareOperationLabel, shareUrl } from '@/util/shareOperations'

type SharePreset = 'download' | 'access' | 'upload'

interface ShareManagementModalProps {
  target: FilesystemRow
  vault: Vault
  onClose: () => void
}

const operationsForPreset = (preset: SharePreset, isDirectory: boolean): ShareOperation[] => {
  if (preset === 'upload') return ['metadata', 'list', 'upload']
  if (preset === 'download') return isDirectory ? ['metadata', 'list', 'download'] : ['metadata', 'download']
  return isDirectory ? ['metadata', 'list', 'download'] : ['metadata', 'download']
}

const linkTypeForPreset = (preset: SharePreset): ShareLinkType => {
  if (preset === 'upload') return 'upload'
  if (preset === 'download') return 'download'
  return 'access'
}

export const ShareManagementModal: React.FC<ShareManagementModalProps> = ({ target, vault, onClose }) => {
  const { shares, loading, error, createShare, fetchShares, revokeShare, rotateToken } = useShareManagementStore()
  const isDirectory = target.entryType === 'directory'
  const [preset, setPreset] = useState<SharePreset>(isDirectory ? 'access' : 'download')
  const [accessMode, setAccessMode] = useState<ShareAccessMode>('public')
  const [label, setLabel] = useState(target.name)
  const [expiresAt, setExpiresAt] = useState('')
  const [oneTimeUrl, setOneTimeUrl] = useState<string | null>(null)
  const [copied, setCopied] = useState(false)

  const targetShares = useMemo(
    () => shares.filter(share => share.vault_id === vault.id && share.root_entry_id === target.id),
    [shares, target.id, vault.id],
  )

  useEffect(() => {
    fetchShares({ vault_id: vault.id }).catch(() => undefined)
  }, [fetchShares, vault.id])

  const copyUrl = async (url: string) => {
    await navigator.clipboard.writeText(url)
    setCopied(true)
    window.setTimeout(() => setCopied(false), 1500)
  }

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    const ops = operationsForPreset(preset, isDirectory)
    const result = await createShare({
      vault_id: vault.id,
      root_entry_id: target.id,
      root_path: target.path || `/${target.name}`,
      target_type: target.entryType,
      link_type: linkTypeForPreset(preset),
      access_mode: accessMode,
      allowed_ops: ops,
      name: label,
      public_label: label,
      expires_at: expiresAt ? new Date(expiresAt).toISOString() : null,
      duplicate_policy: 'reject',
    })
    setOneTimeUrl(shareUrl(result.publicUrlPath))
  }

  const rotate = async (id: string) => {
    const result = await rotateToken(id)
    setOneTimeUrl(shareUrl(result.publicUrlPath))
  }

  return createPortal(
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4 backdrop-blur-sm">
      <section className="w-full max-w-3xl overflow-hidden rounded border border-gray-700 bg-gray-900 text-white shadow-2xl">
        <header className="flex items-start justify-between gap-4 border-b border-gray-800 p-4">
          <div>
            <p className="text-sm text-cyan-300">Share</p>
            <h2 className="text-xl font-semibold">{target.name}</h2>
            <p className="mt-1 text-sm text-gray-400">{target.path || target.name}</p>
          </div>
          <button className="rounded p-2 hover:bg-white/10" onClick={onClose} aria-label="Close share manager">
            <X className="h-5 w-5 fill-current" />
          </button>
        </header>

        <div className="grid gap-0 md:grid-cols-[minmax(0,0.9fr)_minmax(0,1.1fr)]">
          <form className="space-y-3 border-b border-gray-800 p-4 md:border-r md:border-b-0" onSubmit={submit}>
            <label className="block text-sm">
              <span className="mb-1 block text-gray-300">Label</span>
              <input
                className="w-full rounded border border-gray-700 bg-gray-950 px-3 py-2"
                value={label}
                onChange={event => setLabel(event.target.value)}
                required
              />
            </label>

            <label className="block text-sm">
              <span className="mb-1 block text-gray-300">Preset</span>
              <select
                className="w-full rounded border border-gray-700 bg-gray-950 px-3 py-2"
                value={preset}
                onChange={event => setPreset(event.target.value as SharePreset)}>
                <option value="download">Download</option>
                <option value="access">Browse and download</option>
                {isDirectory && <option value="upload">Upload dropbox</option>}
              </select>
            </label>

            <label className="block text-sm">
              <span className="mb-1 block text-gray-300">Access</span>
              <select
                className="w-full rounded border border-gray-700 bg-gray-950 px-3 py-2"
                value={accessMode}
                onChange={event => setAccessMode(event.target.value as ShareAccessMode)}>
                <option value="public">Public</option>
                <option value="email_validated">Email validated</option>
              </select>
            </label>

            <label className="block text-sm">
              <span className="mb-1 block text-gray-300">Expires</span>
              <input
                className="w-full rounded border border-gray-700 bg-gray-950 px-3 py-2"
                type="datetime-local"
                value={expiresAt}
                onChange={event => setExpiresAt(event.target.value)}
              />
            </label>

            <button className="w-full rounded bg-cyan-500 px-3 py-2 font-medium text-gray-950" type="submit" disabled={loading}>
              Create Link
            </button>

            {error && <p className="text-sm text-red-300">{error}</p>}

            {oneTimeUrl && (
              <div className="rounded border border-cyan-500/30 bg-cyan-950/30 p-3 text-sm">
                <div className="mb-2 break-all text-cyan-100">{oneTimeUrl}</div>
                <button className="inline-flex items-center gap-2 rounded bg-cyan-400 px-2 py-1 text-gray-950" type="button" onClick={() => copyUrl(oneTimeUrl)}>
                  <CopyIcon className="h-4 w-4 fill-current" />
                  {copied ? 'Copied' : 'Copy URL'}
                </button>
              </div>
            )}
          </form>

          <div className="max-h-[70vh] overflow-y-auto p-4">
            <div className="mb-3 flex items-center justify-between gap-2">
              <h3 className="font-semibold">Existing Links</h3>
              <button className="rounded border border-gray-700 px-2 py-1 text-sm text-gray-200 hover:bg-white/10" onClick={() => fetchShares({ vault_id: vault.id })}>
                Refresh
              </button>
            </div>

            {targetShares.length === 0 ? (
              <p className="text-sm text-gray-400">No links for this item yet.</p>
            ) : (
              <div className="space-y-3">
                {targetShares.map(share => (
                  <article key={share.id} className="rounded border border-gray-800 bg-gray-950 p-3">
                    <div className="flex items-start justify-between gap-3">
                      <div className="min-w-0">
                        <div className="truncate font-medium">{share.public_label || share.name || share.id}</div>
                        <div className="mt-1 text-xs text-gray-400">
                          {share.link_type} · {share.access_mode} · {managementStatusLabel(share)}
                        </div>
                        <div className="mt-1 text-xs text-gray-500">{shareOperationLabel(share.allowed_ops)}</div>
                      </div>
                      <div className="flex shrink-0 gap-2">
                        <button className="rounded border border-gray-700 p-2 hover:bg-white/10" onClick={() => rotate(share.id)} title="Rotate token">
                          <RotateIcon className="h-4 w-4 fill-current" />
                        </button>
                        <button className="rounded border border-red-700 p-2 text-red-300 hover:bg-red-950" onClick={() => revokeShare(share.id)} title="Revoke">
                          <BanIcon className="h-4 w-4 fill-current" />
                        </button>
                      </div>
                    </div>
                    <div className="mt-2 text-xs text-gray-500">
                      Accesses {share.access_count ?? 0} · Downloads {share.download_count ?? 0} · Uploads {share.upload_count ?? 0}
                    </div>
                  </article>
                ))}
              </div>
            )}
          </div>
        </div>
      </section>
    </div>,
    document.body,
  )
}
