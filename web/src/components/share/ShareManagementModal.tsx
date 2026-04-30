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
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'
import { Vault } from '@/models/vaults'
import { managementStatusLabel, shareOperationLabel, shareUrl } from '@/util/shareOperations'

type SharePreset = 'download' | 'access' | 'upload'

interface ShareManagementModalProps {
  target: FilesystemRow
  vault: Vault
  onClose: () => void
}

const operationsForPreset = (preset: SharePreset, isDirectory: boolean): ShareOperation[] => {
  if (preset === 'upload') return ['metadata', 'list', 'preview', 'upload']
  if (preset === 'download') return isDirectory ? ['metadata', 'list', 'preview', 'download'] : ['metadata', 'preview', 'download']
  return isDirectory ? ['metadata', 'list', 'preview', 'download'] : ['metadata', 'preview', 'download']
}

const linkTypeForPreset = (preset: SharePreset): ShareLinkType => {
  if (preset === 'upload') return 'upload'
  if (preset === 'download') return 'download'
  return 'access'
}

const roleNameForPreset = (preset: SharePreset): string => {
  if (preset === 'upload') return 'share_upload_dropbox'
  if (preset === 'download') return 'share_download_only'
  return 'share_browse_download'
}

type RecipientDraft = { id: string; email: string; roleId: number }

export const ShareManagementModal: React.FC<ShareManagementModalProps> = ({ target, vault, onClose }) => {
  const { shares, loading, error, createShare, fetchShares, revokeShare, rotateToken } = useShareManagementStore()
  const { vaultRoles, fetchVaultRoles } = useVaultRoleStore()
  const isDirectory = target.entryType === 'directory'
  const [preset, setPreset] = useState<SharePreset>(isDirectory ? 'access' : 'download')
  const [accessMode, setAccessMode] = useState<ShareAccessMode>('public')
  const [label, setLabel] = useState(target.name)
  const [expiresAt, setExpiresAt] = useState('')
  const [oneTimeUrl, setOneTimeUrl] = useState<string | null>(null)
  const [copied, setCopied] = useState(false)
  const [publicRoleId, setPublicRoleId] = useState(0)
  const [recipientEmail, setRecipientEmail] = useState('')
  const [recipientRoleId, setRecipientRoleId] = useState(0)
  const [recipients, setRecipients] = useState<RecipientDraft[]>([])

  const roleOptions = useMemo(
    () => vaultRoles.filter(role => role.type === 'vault'),
    [vaultRoles],
  )

  const implicitDenyRole = useMemo(
    () => roleOptions.find(role => role.name === 'implicit_deny') ?? null,
    [roleOptions],
  )

  const shortcutRole = useMemo(
    () => roleOptions.find(role => role.name === roleNameForPreset(preset)) ?? null,
    [preset, roleOptions],
  )

  const targetShares = useMemo(
    () => shares.filter(share => share.vault_id === vault.id && share.root_entry_id === target.id),
    [shares, target.id, vault.id],
  )

  useEffect(() => {
    fetchShares({ vault_id: vault.id }).catch(() => undefined)
    fetchVaultRoles().catch(() => undefined)
  }, [fetchShares, fetchVaultRoles, vault.id])

  useEffect(() => {
    if (shortcutRole) {
      setPublicRoleId(shortcutRole.id)
      if (!recipientRoleId) setRecipientRoleId(shortcutRole.id)
      return
    }
    if (implicitDenyRole && !publicRoleId) setPublicRoleId(implicitDenyRole.id)
    if (implicitDenyRole && !recipientRoleId) setRecipientRoleId(implicitDenyRole.id)
  }, [implicitDenyRole, publicRoleId, recipientRoleId, shortcutRole])

  const copyUrl = async (url: string) => {
    await navigator.clipboard.writeText(url)
    setCopied(true)
    window.setTimeout(() => setCopied(false), 1500)
  }

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    const ops = operationsForPreset(preset, isDirectory)
    const roleId = publicRoleId || implicitDenyRole?.id
    if (!roleId) throw new Error('Implicit deny vault role template is not available.')
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
      public_role_assignment: { vault_role_id: roleId },
      recipient_role_assignments: accessMode === 'email_validated' ?
        recipients
          .filter(recipient => recipient.email.trim() && recipient.roleId)
          .map(recipient => ({
            email: recipient.email.trim(),
            role_assignment: { vault_role_id: recipient.roleId },
          }))
      : [],
    })
    setOneTimeUrl(shareUrl(result.publicUrlPath))
  }

  const rotate = async (id: string) => {
    const result = await rotateToken(id)
    setOneTimeUrl(shareUrl(result.publicUrlPath))
  }

  const addRecipient = () => {
    const email = recipientEmail.trim()
    const roleId = recipientRoleId || publicRoleId || implicitDenyRole?.id || 0
    if (!email || !roleId) return
    setRecipients(current => [
      ...current,
      { id: `${email}-${Date.now()}`, email, roleId },
    ])
    setRecipientEmail('')
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
              <span className="mb-1 block text-gray-300">Permission Shortcut</span>
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
              <span className="mb-1 block text-gray-300">Public Role Template</span>
              <select
                className="w-full rounded border border-gray-700 bg-gray-950 px-3 py-2"
                value={publicRoleId}
                onChange={event => setPublicRoleId(Number(event.target.value))}
                required>
                <option value={0}>Select role template</option>
                {roleOptions.map(role => (
                  <option key={role.id} value={role.id}>{role.name}</option>
                ))}
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

            {accessMode === 'email_validated' && (
              <div className="space-y-2 rounded border border-gray-800 bg-gray-950 p-3 text-sm">
                <div className="font-medium text-gray-200">Email Recipients</div>
                <div className="grid gap-2">
                  <input
                    className="w-full rounded border border-gray-700 bg-gray-900 px-3 py-2"
                    type="email"
                    placeholder="recipient@example.com"
                    value={recipientEmail}
                    onChange={event => setRecipientEmail(event.target.value)}
                  />
                  <select
                    className="w-full rounded border border-gray-700 bg-gray-900 px-3 py-2"
                    value={recipientRoleId}
                    onChange={event => setRecipientRoleId(Number(event.target.value))}>
                    <option value={0}>Select recipient role</option>
                    {roleOptions.map(role => (
                      <option key={role.id} value={role.id}>{role.name}</option>
                    ))}
                  </select>
                  <button
                    className="rounded border border-gray-700 px-2 py-1 text-gray-200 hover:bg-white/10"
                    type="button"
                    onClick={addRecipient}>
                    Add Recipient
                  </button>
                </div>
                {recipients.length > 0 && (
                  <div className="space-y-2">
                    {recipients.map(recipient => {
                      const role = roleOptions.find(candidate => candidate.id === recipient.roleId)
                      return (
                        <div key={recipient.id} className="flex items-center justify-between gap-2 rounded border border-gray-800 px-2 py-1">
                          <span className="min-w-0 truncate text-gray-300">{recipient.email} · {role?.name ?? recipient.roleId}</span>
                          <button
                            className="shrink-0 text-xs text-red-300 hover:text-red-200"
                            type="button"
                            onClick={() => setRecipients(current => current.filter(item => item.id !== recipient.id))}>
                            Remove
                          </button>
                        </div>
                      )
                    })}
                  </div>
                )}
              </div>
            )}

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
