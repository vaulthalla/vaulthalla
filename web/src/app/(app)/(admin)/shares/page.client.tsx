'use client'

import React, { useEffect, useMemo, useState } from 'react'
import CopyIcon from '@/fa-duotone/copy.svg'
import RotateIcon from '@/fa-duotone/rotate-right.svg'
import BanIcon from '@/fa-duotone/ban.svg'
import LinkIcon from '@/fa-duotone/link.svg'
import { AdminPage } from '@/components/admin/AdminPage'
import { useShareManagementStore } from '@/stores/shareManagementStore'
import { useVaultStore } from '@/stores/vaultStore'
import { formatShareDate, managementStatusLabel, shareOperationLabel, shareUrl } from '@/util/shareOperations'

const statusStyles: Record<string, string> = {
  active: 'border-emerald-500/40 bg-emerald-950/30 text-emerald-100',
  revoked: 'border-red-500/40 bg-red-950/40 text-red-100',
  disabled: 'border-gray-600 bg-gray-800 text-gray-300',
  expired: 'border-amber-500/40 bg-amber-950/30 text-amber-100',
}

const shortId = (id: string) => (id.length > 10 ? `${id.slice(0, 8)}...` : id)

const SharesClientPage = () => {
  const { shares, loading, error, fetchShares, revokeShare, rotateToken } = useShareManagementStore()
  const { vaults, fetchVaults } = useVaultStore()
  const [vaultId, setVaultId] = useState<number | ''>('')
  const [oneTimeUrl, setOneTimeUrl] = useState<string | null>(null)
  const [copied, setCopied] = useState(false)

  useEffect(() => {
    fetchVaults().catch(() => undefined)
  }, [fetchVaults])

  useEffect(() => {
    fetchShares({ vault_id: vaultId === '' ? null : vaultId }).catch(() => undefined)
  }, [fetchShares, vaultId])

  const vaultNameById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault.name])), [vaults])

  const rotate = async (id: string) => {
    const result = await rotateToken(id)
    setOneTimeUrl(shareUrl(result.publicUrlPath))
    setCopied(false)
  }

  const copyUrl = async () => {
    if (!oneTimeUrl) return
    await navigator.clipboard.writeText(oneTimeUrl)
    setCopied(true)
    window.setTimeout(() => setCopied(false), 1500)
  }

  return (
    <AdminPage title="Shares" description="Manage active and revoked share links across your vaults.">
      <div className="mx-auto flex w-full max-w-6xl flex-col gap-4 p-4">
        <section className="rounded border border-white/10 bg-gray-900 p-4 text-white">
          <div className="flex flex-col gap-3 md:flex-row md:items-end md:justify-between">
            <label className="flex max-w-sm flex-col gap-1 text-sm">
              Vault
              <select
                className="rounded border border-gray-700 bg-gray-950 px-3 py-2 text-white"
                value={vaultId}
                onChange={event => setVaultId(event.target.value ? Number(event.target.value) : '')}>
                <option value="">All vaults</option>
                {vaults.map(vault => (
                  <option key={vault.id} value={vault.id}>
                    {vault.name}
                  </option>
                ))}
              </select>
            </label>

            <button
              className="rounded border border-gray-700 px-3 py-2 text-sm text-gray-200 hover:bg-white/10"
              onClick={() => fetchShares({ vault_id: vaultId === '' ? null : vaultId }).catch(() => undefined)}>
              Refresh
            </button>
          </div>

          {oneTimeUrl && (
            <div className="mt-4 rounded border border-cyan-500/30 bg-cyan-950/30 p-3 text-sm">
              <p className="text-cyan-100">New public URL from token rotation. Existing URLs are not recoverable after this leaves the screen.</p>
              <div className="mt-2 break-all text-gray-100">{oneTimeUrl}</div>
              <button className="mt-2 inline-flex items-center gap-2 rounded bg-cyan-400 px-2 py-1 text-gray-950" onClick={copyUrl}>
                <CopyIcon className="h-4 w-4 fill-current" />
                {copied ? 'Copied' : 'Copy URL'}
              </button>
            </div>
          )}
        </section>

        {error && <div className="rounded border border-red-500/40 bg-red-950/40 p-3 text-sm text-red-100">{error}</div>}

        <section className="rounded border border-white/10 bg-gray-900 text-white">
          <div className="border-b border-gray-800 p-4">
            <h2 className="font-semibold">Existing share links</h2>
            <p className="mt-1 text-sm text-gray-400">
              Public URLs are intentionally shown only immediately after create or rotate.
            </p>
          </div>

          {loading ? (
            <div className="p-6 text-gray-300">Loading shares...</div>
          ) : shares.length === 0 ? (
            <div className="p-6 text-gray-400">No shares found for this scope.</div>
          ) : (
            <div className="divide-y divide-gray-800">
              {shares.map(share => {
                const status = managementStatusLabel(share)
                return (
                  <article key={share.id} className="grid gap-3 p-4 md:grid-cols-[minmax(0,1.5fr)_minmax(0,1fr)_auto] md:items-center">
                    <div className="min-w-0">
                      <div className="flex min-w-0 items-center gap-2">
                        <LinkIcon className="h-4 w-4 shrink-0 fill-current text-cyan-300" />
                        <h3 className="truncate font-medium">{share.public_label || share.name || shortId(share.id)}</h3>
                      </div>
                      <div className="mt-1 truncate text-sm text-gray-400">{share.root_path}</div>
                      <div className="mt-2 flex flex-wrap gap-2 text-xs text-gray-500">
                        <span>{vaultNameById.get(share.vault_id) || `Vault ${share.vault_id}`}</span>
                        <span>{share.target_type}</span>
                        <span>{share.link_type}</span>
                        <span>{share.access_mode}</span>
                      </div>
                    </div>

                    <div className="text-sm text-gray-300">
                      <div className={`inline-block rounded border px-2 py-1 text-xs ${statusStyles[status] || statusStyles.disabled}`}>
                        {status}
                      </div>
                      <div className="mt-2 text-xs text-gray-500">{shareOperationLabel(share.allowed_ops)}</div>
                      <div className="mt-1 text-xs text-gray-500">Expires: {formatShareDate(share.expires_at)}</div>
                      <div className="mt-1 text-xs text-gray-500">
                        Accesses {share.access_count ?? 0} · Downloads {share.download_count ?? 0} · Uploads {share.upload_count ?? 0}
                      </div>
                    </div>

                    <div className="flex gap-2 md:justify-end">
                      <button className="rounded border border-gray-700 p-2 hover:bg-white/10" title="Rotate token" onClick={() => rotate(share.id)}>
                        <RotateIcon className="h-4 w-4 fill-current" />
                      </button>
                      <button
                        className="rounded border border-red-700 p-2 text-red-300 hover:bg-red-950 disabled:opacity-50"
                        title="Revoke"
                        disabled={status === 'revoked'}
                        onClick={() => revokeShare(share.id)}>
                        <BanIcon className="h-4 w-4 fill-current" />
                      </button>
                    </div>
                  </article>
                )
              })}
            </div>
          )}
        </section>
      </div>
    </AdminPage>
  )
}

export default SharesClientPage
