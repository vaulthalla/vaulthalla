'use client'

import React, { useState } from 'react'
import { useWs, invalidate } from '@/lib/query'
import { api } from '@/lib/session'
import type { ShareLink } from '@/models/linkShare'
import { Badge } from '@/components/ui/Badge'
import { Button } from '@/components/ui/Button'
import { DropdownMenu } from '@/components/ui/Menu'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { QueryState, EmptyState } from '@/components/ui/State'
import { CopyIcon, EllipsisIcon, ArrowsRotateIcon, BanIcon, LinkIcon, CheckIcon } from '@/components/ui/icons'
import { formatDate, formatInt, formatRelative } from '@/lib/format'
import { describeOps, publicUrl, shareState, shareStateTone } from '@/features/shares/shareMeta'

// The one-time URL, shown right after create or rotate (the server never returns it again).
export const OneTimeUrl = ({ url, onDismiss }: { url: string; onDismiss?: () => void }) => {
  const [copied, setCopied] = useState(false)
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(url)
      setCopied(true)
      setTimeout(() => setCopied(false), 1600)
    } catch {
      notify.info('Copy the link manually', 'Clipboard access isn’t available here.')
    }
  }
  return (
    <div className="rounded-card border border-accent-line bg-accent-soft p-3.5">
      <div className="mb-2 flex items-center gap-2 text-sm font-medium text-fg">
        <LinkIcon className="size-4 text-accent-text" aria-hidden />
        Link ready — copy it now
        {onDismiss ? (
          <button type="button" className="ml-auto text-xs text-fg-subtle hover:text-fg" onClick={onDismiss}>
            Dismiss
          </button>
        ) : null}
      </div>
      <div className="flex items-center gap-2">
        <code className="min-w-0 flex-1 truncate rounded-md border border-line bg-black/40 px-2.5 py-2 font-mono text-xs text-fg" title={url}>
          {url}
        </code>
        <Button size="sm" variant="primary" onClick={copy}>
          {copied ? <CheckIcon aria-hidden /> : <CopyIcon aria-hidden />}
          {copied ? 'Copied' : 'Copy'}
        </Button>
      </div>
      <p className="mt-2 text-xs text-fg-subtle">For security, the full link is only shown once. Rotate it later to get a new one.</p>
    </div>
  )
}

export const useShareActions = (onUrl: (url: string) => void) => {
  const rotate = async (share: ShareLink) => {
    const ok = await confirm({
      title: 'Rotate this link?',
      description: 'Anyone using the current link loses access immediately. You’ll get a new link to send.',
      confirmLabel: 'Rotate link',
    })
    if (!ok) return
    try {
      const res = await api.send('share.link.rotate_token', { id: share.id })
      onUrl(publicUrl(res.public_url_path))
      await invalidate('share.link.list')
      notify.success('Link rotated')
    } catch (error) {
      notify.error(error, 'Could not rotate the link')
    }
  }
  const revoke = async (share: ShareLink) => {
    const ok = await confirm({
      title: 'Revoke this link?',
      description: 'The link stops working for everyone, permanently.',
      confirmLabel: 'Revoke',
    })
    if (!ok) return
    try {
      await api.send('share.link.revoke', { id: share.id })
      await invalidate('share.link.list')
      notify.success('Link revoked')
    } catch (error) {
      notify.error(error, 'Could not revoke the link')
    }
  }
  return { rotate, revoke }
}

export const ShareLinkRow = ({ share, vaultName, onUrl }: { share: ShareLink; vaultName?: string; onUrl: (url: string) => void }) => {
  const state = shareState(share)
  const { rotate, revoke } = useShareActions(onUrl)
  return (
    <div className="flex items-start gap-3 border-b border-line/60 px-4 py-3 last:border-0">
      <div className="min-w-0 flex-1">
        <div className="flex flex-wrap items-center gap-x-2 gap-y-1">
          <span className="min-w-0 truncate font-medium text-fg">{share.public_label || share.name || share.root_path}</span>
          <Badge tone={shareStateTone[state]}>{state}</Badge>
          <Badge tone="neutral">{describeOps(share.allowed_ops)}</Badge>
          {share.access_mode === 'email_validated' ? <Badge tone="info">email verified</Badge> : null}
        </div>
        <div className="mt-1 truncate text-xs text-fg-subtle">
          {vaultName ? `${vaultName} · ` : ''}
          <span className="font-mono">{share.root_path}</span>
        </div>
        <div className="mt-1 flex flex-wrap gap-x-3 gap-y-0.5 text-xs text-fg-faint tabular">
          <span>
            {formatInt(share.access_count ?? 0)} opens · {formatInt(share.download_count ?? 0)} downloads · {formatInt(share.upload_count ?? 0)} uploads
          </span>
          <span>{share.expires_at ? `Expires ${formatDate(share.expires_at)}` : 'No expiry'}</span>
          <span>Last used {formatRelative(share.last_accessed_at, 'never')}</span>
        </div>
      </div>
      {state === 'revoked' ? null : (
        <DropdownMenu
          label="Link actions"
          entries={[
            { key: 'rotate', label: 'Rotate link…', icon: ArrowsRotateIcon, onSelect: () => void rotate(share) },
            { key: 'revoke', label: 'Revoke…', icon: BanIcon, danger: true, onSelect: () => void revoke(share) },
          ]}
          trigger={
            <button type="button" aria-label="Link actions" className="grid size-8 shrink-0 place-items-center rounded-md text-fg-subtle hover:bg-surface-3 hover:text-fg">
              <EllipsisIcon className="size-4" aria-hidden />
            </button>
          }
        />
      )}
    </div>
  )
}

export const ShareLinkList = ({
  vaultId,
  rootEntryId,
  vaultNames,
  emptyText = 'No share links yet.',
}: {
  vaultId?: number
  rootEntryId?: number
  vaultNames?: Map<number, string>
  emptyText?: string
}) => {
  const query = useWs('share.link.list', vaultId ? { vault_id: vaultId, limit: 500 } : { limit: 500 })
  const [url, setUrl] = useState<string | null>(null)
  return (
    <div className="space-y-3">
      {url ? <OneTimeUrl url={url} onDismiss={() => setUrl(null)} /> : null}
      <QueryState query={query}>
        {data => {
          const shares = (data.shares ?? []).filter(s => rootEntryId === undefined || s.root_entry_id === rootEntryId)
          if (!shares.length) return <EmptyState icon={LinkIcon} title={emptyText} />
          return (
            <div className="panel overflow-hidden">
              {shares.map(share => (
                <ShareLinkRow key={share.id} share={share} vaultName={vaultNames?.get(share.vault_id)} onUrl={setUrl} />
              ))}
            </div>
          )
        }}
      </QueryState>
    </div>
  )
}
