'use client'

import React, { FormEvent, useEffect, useMemo, useState } from 'react'
import { createPortal } from 'react-dom'
import X from '@/fa-duotone/x.svg'
import CopyIcon from '@/fa-duotone/copy.svg'
import RotateIcon from '@/fa-duotone/rotate-right.svg'
import BanIcon from '@/fa-duotone/ban.svg'
import CircleNotch from '@/fa-duotone/circle-notch.svg'
import DownloadIcon from '@/fa-duotone/download.svg'
import UploadIcon from '@/fa-duotone/upload.svg'
import ShieldCheckIcon from '@/fa-duotone/shield-check.svg'
import GlobeIcon from '@/fa-duotone/globe.svg'
import EnvelopeIcon from '@/fa-duotone/envelope.svg'
import LinkIcon from '@/fa-duotone/link-simple.svg'
import CheckIcon from '@/fa-duotone/check.svg'
import { FilesystemRow } from '@/components/fs/types'
import { ShareLink, ShareLinkType, ShareAccessMode, ShareOperation } from '@/models/linkShare'
import { VaultRole } from '@/models/role'
import { useShareManagementStore } from '@/stores/shareManagementStore'
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'
import { Vault } from '@/models/vaults'
import { formatShareDate, managementStatusLabel, shareOperationLabel, shareUrl, statusForShare } from '@/util/shareOperations'

type SharePreset = 'access' | 'download' | 'upload'
type ShareShortcut = SharePreset | 'custom'
type RecipientDraft = { id: string; email: string; roleId: number }

interface ShareManagementModalProps {
  target: FilesystemRow
  vault: Vault
  onClose: () => void
}

const cx = (...parts: Array<string | false | null | undefined>) => parts.filter(Boolean).join(' ')

const operationsForPreset = (preset: SharePreset, isDirectory: boolean): ShareOperation[] => {
  if (preset === 'upload') return ['metadata', 'list', 'upload']
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

const shortcutDetails: Record<ShareShortcut, { title: string; description: string; hint: string; icon: React.ComponentType<{ className?: string }> }> = {
  access: {
    title: 'Browse + Download',
    description: 'Browse folders, preview files, and download protected contents.',
    hint: 'Scoped read surface',
    icon: ShieldCheckIcon,
  },
  download: {
    title: 'Download Only',
    description: 'A direct download lane with only the minimal metadata needed.',
    hint: 'File-first handoff',
    icon: DownloadIcon,
  },
  upload: {
    title: 'Upload Dropbox',
    description: 'Let recipients deposit files into this directory without browse-heavy access.',
    hint: 'Directory only',
    icon: UploadIcon,
  },
  custom: {
    title: 'Custom',
    description: 'Manual role/template selection is active for this link draft.',
    hint: 'Manual control',
    icon: ShieldCheckIcon,
  },
}

const roleLabel = (role?: VaultRole | null) => role?.name.replace(/_/g, ' ') ?? 'No template selected'

const shortcutRoleName = (shortcut: ShareShortcut) => shortcut === 'custom' ? null : roleNameForPreset(shortcut)

const isPublicShareRoute = () => typeof window !== 'undefined' && window.location.pathname.startsWith('/share/')

const matchesShortcutRole = (shortcut: ShareShortcut, roleId: number, rolesByShortcut: Record<SharePreset, VaultRole | null>) => {
  if (shortcut === 'custom') return false
  return rolesByShortcut[shortcut]?.id === roleId
}

const statusClasses = (share: ShareLink) => {
  const status = managementStatusLabel(share)
  if (status === 'revoked') return 'border-red-400/30 bg-red-500/10 text-red-100'
  if (status === 'expired' || status === 'disabled') return 'border-amber-300/30 bg-amber-500/10 text-amber-100'
  return 'border-emerald-300/30 bg-emerald-500/10 text-emerald-100'
}

const ShortcutCard = ({
  shortcut,
  selected,
  disabled,
  onClick,
}: {
  shortcut: ShareShortcut
  selected: boolean
  disabled?: boolean
  onClick: () => void
}) => {
  const details = shortcutDetails[shortcut]
  const Icon = details.icon

  return (
    <button
      type="button"
      disabled={disabled}
      onClick={onClick}
      className={cx(
        'group flex min-h-32 flex-col rounded-lg border p-3 text-left transition',
        selected ?
          'border-cyan-300/60 bg-cyan-400/10 shadow-[0_0_30px_rgba(34,211,238,0.14)]'
        : 'border-white/10 bg-black/20 hover:border-cyan-300/35 hover:bg-cyan-400/5',
        disabled && 'cursor-not-allowed opacity-45 hover:border-white/10 hover:bg-black/20',
      )}>
      <span className="flex items-center justify-between gap-2">
        <span className={cx('flex h-8 w-8 items-center justify-center rounded border', selected ? 'border-cyan-300/40 bg-cyan-300/15 text-cyan-100' : 'border-white/10 bg-white/5 text-white/70')}>
          <Icon className="h-4 w-4 fill-current" />
        </span>
        <span className={cx('rounded-full border px-2 py-0.5 text-[10px] uppercase tracking-[0.16em]', selected ? 'border-cyan-300/35 text-cyan-100' : 'border-white/10 text-white/35')}>
          {selected ? 'Selected' : details.hint}
        </span>
      </span>
      <span className="mt-3 text-sm font-semibold text-white">{details.title}</span>
      <span className="mt-1 text-xs leading-5 text-white/55">{disabled ? 'Available for directory shares.' : details.description}</span>
    </button>
  )
}

const AccessToggle = ({
  value,
  onChange,
}: {
  value: ShareAccessMode
  onChange: (value: ShareAccessMode) => void
}) => {
  const options: Array<{ value: ShareAccessMode; title: string; detail: string; icon: React.ComponentType<{ className?: string }> }> = [
    { value: 'public', title: 'Public', detail: 'Anyone with the link', icon: GlobeIcon },
    { value: 'email_validated', title: 'Email validated', detail: 'Recipients verify inbox access', icon: EnvelopeIcon },
  ]

  return (
    <div className="grid grid-cols-2 gap-2 rounded-lg border border-white/10 bg-black/30 p-1">
      {options.map(option => {
        const Icon = option.icon
        const selected = value === option.value
        return (
          <button
            key={option.value}
            type="button"
            onClick={() => onChange(option.value)}
            className={cx(
              'flex items-center gap-2 rounded-md px-3 py-2 text-left transition',
              selected ? 'bg-cyan-400/15 text-cyan-50 shadow-[inset_0_0_0_1px_rgba(103,232,249,0.35)]' : 'text-white/55 hover:bg-white/5 hover:text-white',
            )}>
            <Icon className="h-4 w-4 shrink-0 fill-current" />
            <span className="min-w-0">
              <span className="block text-sm font-semibold">{option.title}</span>
              <span className="block truncate text-[11px] opacity-70">{option.detail}</span>
            </span>
          </button>
        )
      })}
    </div>
  )
}

const GeneratedLinkCard = ({
  url,
  copied,
  onCopy,
}: {
  url: string
  copied: boolean
  onCopy: () => void
}) => (
  <div className="rounded-lg border border-cyan-300/30 bg-cyan-400/10 p-3 shadow-[0_0_40px_rgba(34,211,238,0.08)]">
    <div className="mb-2 flex items-center justify-between gap-3">
      <div>
        <div className="text-sm font-semibold text-cyan-50">One-time URL</div>
        <div className="text-xs text-cyan-100/55">Shown only after create or rotate.</div>
      </div>
      <LinkIcon className="h-5 w-5 fill-current text-cyan-200" />
    </div>
    <div className="flex items-center gap-2 rounded border border-white/10 bg-black/35 p-2">
      <code className="min-w-0 flex-1 overflow-hidden text-ellipsis whitespace-nowrap font-mono text-xs text-cyan-50">{url}</code>
      <button className="inline-flex shrink-0 items-center gap-2 rounded bg-cyan-300 px-3 py-1.5 text-xs font-semibold text-gray-950 transition hover:bg-cyan-200" type="button" onClick={onCopy}>
        {copied ? <CheckIcon className="h-3.5 w-3.5 fill-current" /> : <CopyIcon className="h-3.5 w-3.5 fill-current" />}
        {copied ? 'Copied' : 'Copy'}
      </button>
    </div>
  </div>
)

export const ShareManagementModal: React.FC<ShareManagementModalProps> = ({ target, vault, onClose }) => {
  const { shares, loading, error, createShare, fetchShares, revokeShare, rotateToken } = useShareManagementStore()
  const { vaultRoles, fetchVaultRoles } = useVaultRoleStore()
  const isDirectory = target.entryType === 'directory'
  const defaultPreset: SharePreset = isDirectory ? 'access' : 'download'
  const [shortcut, setShortcut] = useState<ShareShortcut>(defaultPreset)
  const [linkProfile, setLinkProfile] = useState<SharePreset>(defaultPreset)
  const [accessMode, setAccessMode] = useState<ShareAccessMode>('public')
  const [label, setLabel] = useState(target.name)
  const [expiresAt, setExpiresAt] = useState('')
  const [oneTimeUrl, setOneTimeUrl] = useState<string | null>(null)
  const [copied, setCopied] = useState(false)
  const [publicRoleId, setPublicRoleId] = useState(0)
  const [recipientEmail, setRecipientEmail] = useState('')
  const [recipientRoleId, setRecipientRoleId] = useState(0)
  const [recipients, setRecipients] = useState<RecipientDraft[]>([])
  const [isCreating, setIsCreating] = useState(false)
  const [isRefreshing, setIsRefreshing] = useState(false)
  const [rotatingId, setRotatingId] = useState<string | null>(null)
  const [revokingId, setRevokingId] = useState<string | null>(null)
  const [formError, setFormError] = useState<string | null>(null)

  const roleOptions = useMemo(
    () => vaultRoles.filter(role => role.type === 'vault'),
    [vaultRoles],
  )

  const implicitDenyRole = useMemo(
    () => roleOptions.find(role => role.name === 'implicit_deny') ?? null,
    [roleOptions],
  )

  const rolesByShortcut = useMemo<Record<SharePreset, VaultRole | null>>(
    () => ({
      access: roleOptions.find(role => role.name === roleNameForPreset('access')) ?? null,
      download: roleOptions.find(role => role.name === roleNameForPreset('download')) ?? null,
      upload: roleOptions.find(role => role.name === roleNameForPreset('upload')) ?? null,
    }),
    [roleOptions],
  )

  const publicRole = useMemo(
    () => roleOptions.find(role => role.id === publicRoleId) ?? null,
    [publicRoleId, roleOptions],
  )

  const targetShares = useMemo(
    () => shares.filter(share => share.vault_id === vault.id && share.root_entry_id === target.id),
    [shares, target.id, vault.id],
  )

  const currentOps = useMemo(() => operationsForPreset(linkProfile, isDirectory), [isDirectory, linkProfile])
  const targetPath = target.path || `/${target.name}`
  const selectedShortcutRoleName = shortcutRoleName(shortcut)

  useEffect(() => {
    if (isPublicShareRoute()) return

    let mounted = true
    const init = async () => {
      setIsRefreshing(true)
      try {
        await Promise.all([
          fetchShares({ vault_id: vault.id }),
          fetchVaultRoles(),
        ])
      } finally {
        if (mounted) setIsRefreshing(false)
      }
    }

    init().catch(() => undefined)
    return () => {
      mounted = false
    }
  }, [fetchShares, fetchVaultRoles, vault.id])

  useEffect(() => {
    if (!publicRoleId) {
      const role = shortcut !== 'custom' ? rolesByShortcut[shortcut] : null
      if (role) setPublicRoleId(role.id)
      else if (implicitDenyRole) setPublicRoleId(implicitDenyRole.id)
    }

    if (!recipientRoleId) {
      const role = shortcut !== 'custom' ? rolesByShortcut[shortcut] : null
      if (role) setRecipientRoleId(role.id)
      else if (publicRoleId) setRecipientRoleId(publicRoleId)
      else if (implicitDenyRole) setRecipientRoleId(implicitDenyRole.id)
    }
  }, [implicitDenyRole, publicRoleId, recipientRoleId, rolesByShortcut, shortcut])

  const markCustomIfAwayFromShortcut = (roleId: number) => {
    if (shortcut !== 'custom' && !matchesShortcutRole(shortcut, roleId, rolesByShortcut)) setShortcut('custom')
  }

  const applyShortcut = (nextShortcut: SharePreset) => {
    if (nextShortcut === 'upload' && !isDirectory) return
    setShortcut(nextShortcut)
    setLinkProfile(nextShortcut)
    setFormError(null)
    const role = rolesByShortcut[nextShortcut]
    if (role) {
      setPublicRoleId(role.id)
      setRecipientRoleId(role.id)
    }
  }

  const copyUrl = async (url: string) => {
    await navigator.clipboard.writeText(url)
    setCopied(true)
    window.setTimeout(() => setCopied(false), 1500)
  }

  const refreshLinks = async () => {
    setIsRefreshing(true)
    try {
      await fetchShares({ vault_id: vault.id })
    } finally {
      setIsRefreshing(false)
    }
  }

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    setFormError(null)
    const roleId = publicRoleId || implicitDenyRole?.id
    if (!roleId) {
      setFormError('A vault role template is required before creating a link.')
      return
    }

    setIsCreating(true)
    try {
      const result = await createShare({
        vault_id: vault.id,
        root_entry_id: target.id,
        root_path: targetPath,
        target_type: target.entryType,
        link_type: linkTypeForPreset(linkProfile),
        access_mode: accessMode,
        allowed_ops: currentOps,
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
    } catch (err) {
      setFormError(err instanceof Error ? err.message : 'Unable to create share link.')
    } finally {
      setIsCreating(false)
    }
  }

  const rotate = async (id: string) => {
    setRotatingId(id)
    setFormError(null)
    try {
      const result = await rotateToken(id)
      setOneTimeUrl(shareUrl(result.publicUrlPath))
    } catch (err) {
      setFormError(err instanceof Error ? err.message : 'Unable to rotate link.')
    } finally {
      setRotatingId(null)
    }
  }

  const revoke = async (id: string) => {
    setRevokingId(id)
    setFormError(null)
    try {
      await revokeShare(id)
    } catch (err) {
      setFormError(err instanceof Error ? err.message : 'Unable to revoke link.')
    } finally {
      setRevokingId(null)
    }
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
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/80 p-3 text-white backdrop-blur-md sm:p-4">
      <section className="relative flex max-h-[92vh] w-full max-w-6xl flex-col overflow-hidden rounded-lg border border-cyan-300/20 bg-[#05080d] shadow-[0_28px_90px_rgba(0,0,0,0.75),0_0_45px_rgba(34,211,238,0.08)]">
        <div className="absolute inset-x-0 top-0 h-px bg-linear-to-r from-transparent via-cyan-300/70 to-transparent" />
        <header className="flex items-start justify-between gap-4 border-b border-white/10 bg-white/[0.03] p-4 sm:p-5">
          <div className="min-w-0">
            <div className="mb-2 inline-flex items-center gap-2 rounded-full border border-cyan-300/20 bg-cyan-300/10 px-2.5 py-1 text-[10px] font-bold uppercase tracking-[0.18em] text-cyan-100">
              <LinkIcon className="h-3 w-3 fill-current" />
              Share
            </div>
            <h2 className="truncate text-2xl font-semibold text-white">{target.name}</h2>
            <p className="mt-1 truncate font-mono text-xs text-white/45">{targetPath}</p>
          </div>
          <button className="rounded-md border border-white/10 bg-white/5 p-2 text-white/70 transition hover:bg-white/10 hover:text-white" onClick={onClose} aria-label="Close share manager">
            <X className="h-5 w-5 fill-current" />
          </button>
        </header>

        <div className="grid min-h-0 flex-1 overflow-hidden lg:grid-cols-[minmax(0,0.94fr)_minmax(360px,1.06fr)]">
          <form className="min-h-0 space-y-4 overflow-y-auto border-b border-white/10 p-4 lg:border-r lg:border-b-0 sm:p-5" onSubmit={submit}>
            <section className="rounded-lg border border-white/10 bg-white/[0.035] p-4">
              <div className="mb-4 flex items-start justify-between gap-3">
                <div>
                  <h3 className="text-sm font-semibold uppercase tracking-[0.14em] text-cyan-100">Create Link</h3>
                  <p className="mt-1 text-sm text-white/50">Choose the share surface, then generate a one-time URL.</p>
                </div>
                <span className="rounded-full border border-white/10 px-2.5 py-1 text-[10px] uppercase tracking-[0.16em] text-white/40">
                  {target.entryType}
                </span>
              </div>

              <div className="space-y-4">
                <label className="block text-sm">
                  <span className="mb-1.5 block text-xs font-semibold uppercase tracking-[0.14em] text-white/45">Display Label</span>
                  <input
                    className="w-full rounded-md border border-white/10 bg-black/35 px-3 py-2.5 text-white outline-none transition placeholder:text-white/25 focus:border-cyan-300/45 focus:ring-2 focus:ring-cyan-300/10"
                    value={label}
                    onChange={event => setLabel(event.target.value)}
                    required
                  />
                </label>

                <div>
                  <div className="mb-2 flex items-center justify-between gap-3">
                    <span className="text-xs font-semibold uppercase tracking-[0.14em] text-white/45">Permission Shortcut</span>
                    <span className={cx('rounded-full border px-2 py-0.5 text-[10px] uppercase tracking-[0.16em]', shortcut === 'custom' ? 'border-amber-300/30 text-amber-100' : 'border-cyan-300/30 text-cyan-100')}>
                      {shortcutDetails[shortcut].title}
                    </span>
                  </div>
                  <div className="grid gap-2 sm:grid-cols-2">
                    {(['access', 'download', 'upload'] as SharePreset[]).map(candidate => (
                      <ShortcutCard
                        key={candidate}
                        shortcut={candidate}
                        selected={shortcut === candidate}
                        disabled={candidate === 'upload' && !isDirectory}
                        onClick={() => applyShortcut(candidate)}
                      />
                    ))}
                    <ShortcutCard
                      shortcut="custom"
                      selected={shortcut === 'custom'}
                      onClick={() => setShortcut('custom')}
                    />
                  </div>
                </div>

                <div className="rounded-lg border border-white/10 bg-black/25 p-3">
                  <div className="mb-2 flex items-center justify-between gap-3">
                    <div>
                      <span className="block text-xs font-semibold uppercase tracking-[0.14em] text-white/45">Share Template</span>
                      <span className="mt-0.5 block text-xs text-white/35">
                        {selectedShortcutRoleName ? `Shortcut role: ${selectedShortcutRoleName}` : 'Manual role selection'}
                      </span>
                    </div>
                    {shortcut === 'custom' && <span className="rounded-full border border-amber-300/30 bg-amber-300/10 px-2 py-0.5 text-[10px] uppercase tracking-[0.16em] text-amber-100">Custom</span>}
                  </div>
                  <select
                    className="w-full rounded-md border border-white/10 bg-gray-950 px-3 py-2.5 text-sm text-white outline-none transition focus:border-cyan-300/45 focus:ring-2 focus:ring-cyan-300/10"
                    value={publicRoleId}
                    onChange={event => {
                      const nextRoleId = Number(event.target.value)
                      setPublicRoleId(nextRoleId)
                      markCustomIfAwayFromShortcut(nextRoleId)
                    }}
                    required>
                    <option value={0}>Select role template</option>
                    {roleOptions.map(role => (
                      <option key={role.id} value={role.id}>{role.name}</option>
                    ))}
                  </select>
                  <div className="mt-2 rounded border border-white/10 bg-white/[0.03] px-3 py-2 text-xs text-white/45">
                    Selected: <span className="text-white/75">{roleLabel(publicRole)}</span>
                  </div>
                </div>

                <div>
                  <span className="mb-2 block text-xs font-semibold uppercase tracking-[0.14em] text-white/45">Access Mode</span>
                  <AccessToggle value={accessMode} onChange={setAccessMode} />
                </div>

                {accessMode === 'email_validated' && (
                  <div className="space-y-3 rounded-lg border border-cyan-300/15 bg-cyan-300/[0.04] p-3">
                    <div>
                      <div className="text-sm font-semibold text-cyan-50">Email Recipients</div>
                      <p className="mt-1 text-xs text-white/45">Recipients validate inbox access before the share opens.</p>
                    </div>
                    <div className="grid gap-2">
                      <input
                        className="w-full rounded-md border border-white/10 bg-black/35 px-3 py-2.5 text-sm text-white outline-none placeholder:text-white/25 focus:border-cyan-300/45 focus:ring-2 focus:ring-cyan-300/10"
                        type="email"
                        placeholder="recipient@example.com"
                        value={recipientEmail}
                        onChange={event => setRecipientEmail(event.target.value)}
                      />
                      <select
                        className="w-full rounded-md border border-white/10 bg-gray-950 px-3 py-2.5 text-sm text-white outline-none focus:border-cyan-300/45 focus:ring-2 focus:ring-cyan-300/10"
                        value={recipientRoleId}
                        onChange={event => {
                          const nextRoleId = Number(event.target.value)
                          setRecipientRoleId(nextRoleId)
                          markCustomIfAwayFromShortcut(nextRoleId)
                        }}>
                        <option value={0}>Select recipient role</option>
                        {roleOptions.map(role => (
                          <option key={role.id} value={role.id}>{role.name}</option>
                        ))}
                      </select>
                      <button
                        className="rounded-md border border-cyan-300/25 bg-cyan-300/10 px-3 py-2 text-sm font-semibold text-cyan-50 transition hover:bg-cyan-300/15 disabled:cursor-not-allowed disabled:opacity-45"
                        type="button"
                        disabled={!recipientEmail.trim() || !recipientRoleId}
                        onClick={addRecipient}>
                        Add Recipient
                      </button>
                    </div>
                    {recipients.length > 0 && (
                      <div className="space-y-2">
                        {recipients.map(recipient => {
                          const role = roleOptions.find(candidate => candidate.id === recipient.roleId)
                          return (
                            <div key={recipient.id} className="flex items-center justify-between gap-2 rounded-md border border-white/10 bg-black/25 px-3 py-2">
                              <span className="min-w-0 truncate text-xs text-white/70">{recipient.email} · {roleLabel(role)}</span>
                              <button
                                className="shrink-0 text-xs font-semibold text-red-200 transition hover:text-red-100"
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
                  <span className="mb-1.5 block text-xs font-semibold uppercase tracking-[0.14em] text-white/45">Expires</span>
                  <input
                    className="w-full rounded-md border border-white/10 bg-black/35 px-3 py-2.5 text-white outline-none transition focus:border-cyan-300/45 focus:ring-2 focus:ring-cyan-300/10"
                    type="datetime-local"
                    value={expiresAt}
                    onChange={event => setExpiresAt(event.target.value)}
                  />
                </label>
              </div>
            </section>

            {(formError || error) && (
              <div className="rounded-lg border border-red-400/30 bg-red-500/10 px-3 py-2 text-sm text-red-100">
                {formError || error}
              </div>
            )}

            {oneTimeUrl && (
              <GeneratedLinkCard url={oneTimeUrl} copied={copied} onCopy={() => copyUrl(oneTimeUrl).catch(() => undefined)} />
            )}

            <button
              className="flex w-full items-center justify-center gap-2 rounded-md bg-cyan-300 px-4 py-3 text-sm font-bold text-gray-950 shadow-[0_0_35px_rgba(34,211,238,0.18)] transition hover:bg-cyan-200 disabled:cursor-not-allowed disabled:opacity-60"
              type="submit"
              disabled={isCreating || !publicRoleId}>
              {isCreating && <CircleNotch className="h-4 w-4 animate-spin fill-current" />}
              {isCreating ? 'Forging Link...' : 'Create Link'}
            </button>
          </form>

          <aside className="min-h-0 overflow-y-auto bg-black/20 p-4 sm:p-5">
            <div className="mb-4 flex items-center justify-between gap-3">
              <div>
                <h3 className="text-sm font-semibold uppercase tracking-[0.14em] text-cyan-100">Existing Links</h3>
                <p className="mt-1 text-sm text-white/45">{targetShares.length} link{targetShares.length === 1 ? '' : 's'} for this item</p>
              </div>
              <button
                className="inline-flex items-center gap-2 rounded-md border border-white/10 bg-white/5 px-3 py-2 text-sm text-white/70 transition hover:bg-white/10 hover:text-white disabled:cursor-not-allowed disabled:opacity-50"
                type="button"
                disabled={isRefreshing}
                onClick={() => refreshLinks().catch(() => undefined)}>
                {isRefreshing && <CircleNotch className="h-3.5 w-3.5 animate-spin fill-current" />}
                Refresh
              </button>
            </div>

            {isRefreshing && targetShares.length === 0 ? (
              <div className="rounded-lg border border-white/10 bg-white/[0.03] p-6 text-center text-sm text-white/55">
                <CircleNotch className="mx-auto mb-3 h-6 w-6 animate-spin fill-current text-cyan-200" />
                Loading share links...
              </div>
            ) : targetShares.length === 0 ? (
              <div className="rounded-lg border border-dashed border-white/15 bg-white/[0.03] p-6 text-center">
                <LinkIcon className="mx-auto mb-3 h-7 w-7 fill-current text-white/25" />
                <p className="text-sm font-semibold text-white/70">No links for this item yet.</p>
                <p className="mt-1 text-xs text-white/40">Create one and its one-time URL will appear on the left.</p>
              </div>
            ) : (
              <div className="space-y-3">
                {targetShares.map(share => {
                  const status = managementStatusLabel(share)
                  const busy = rotatingId === share.id || revokingId === share.id
                  return (
                    <article key={share.id} className="rounded-lg border border-white/10 bg-white/[0.035] p-3 transition hover:border-cyan-300/25">
                      <div className="flex items-start justify-between gap-3">
                        <div className="min-w-0">
                          <div className="truncate text-sm font-semibold text-white">{share.public_label || share.name || share.id}</div>
                          <div className="mt-1 flex flex-wrap items-center gap-2 text-[11px] text-white/45">
                            <span className="rounded-full border border-white/10 px-2 py-0.5 uppercase tracking-[0.14em]">{share.link_type}</span>
                            <span className="rounded-full border border-white/10 px-2 py-0.5 uppercase tracking-[0.14em]">{share.access_mode.replace('_', ' ')}</span>
                            <span className={cx('rounded-full border px-2 py-0.5 uppercase tracking-[0.14em]', statusClasses(share))}>{status}</span>
                          </div>
                        </div>
                        <div className="flex shrink-0 gap-2">
                          <button
                            className="rounded-md border border-white/10 bg-white/5 p-2 text-white/70 transition hover:bg-white/10 hover:text-cyan-100 disabled:cursor-not-allowed disabled:opacity-45"
                            type="button"
                            disabled={busy || statusForShare(share) === 'revoked'}
                            onClick={() => rotate(share.id)}
                            title="Rotate token">
                            {rotatingId === share.id ? <CircleNotch className="h-4 w-4 animate-spin fill-current" /> : <RotateIcon className="h-4 w-4 fill-current" />}
                          </button>
                          <button
                            className="rounded-md border border-red-400/25 bg-red-500/10 p-2 text-red-200 transition hover:bg-red-500/15 disabled:cursor-not-allowed disabled:opacity-45"
                            type="button"
                            disabled={busy || statusForShare(share) === 'revoked'}
                            onClick={() => revoke(share.id)}
                            title="Revoke">
                            {revokingId === share.id ? <CircleNotch className="h-4 w-4 animate-spin fill-current" /> : <BanIcon className="h-4 w-4 fill-current" />}
                          </button>
                        </div>
                      </div>

                      <div className="mt-3 rounded-md border border-white/10 bg-black/25 px-3 py-2 text-xs text-white/45">
                        <div className="truncate">{shareOperationLabel(share.allowed_ops)}</div>
                        <div className="mt-1 flex flex-wrap gap-x-3 gap-y-1 text-[11px] text-white/35">
                          <span>Accesses {share.access_count ?? 0}</span>
                          <span>Downloads {share.download_count ?? 0}</span>
                          <span>Uploads {share.upload_count ?? 0}</span>
                          <span>{formatShareDate(share.expires_at)}</span>
                        </div>
                      </div>
                    </article>
                  )
                })}
              </div>
            )}

            {loading && !isRefreshing && (
              <div className="mt-3 rounded-md border border-white/10 bg-white/[0.03] px-3 py-2 text-xs text-white/45">
                Syncing share state...
              </div>
            )}
          </aside>
        </div>
      </section>
    </div>,
    document.body,
  )
}
