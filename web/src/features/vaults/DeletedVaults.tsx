'use client'

import React, { useState } from 'react'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { ArrowsRotateIcon, TriangleExclamationIcon } from '@/components/ui/icons'
import { formatDateTime, formatRelative } from '@/lib/format'
import { invalidate, useWs } from '@/lib/query'
import { api } from '@/lib/session'
import { isWsError } from '@/lib/ws/errors'
import type { VaultDeletion } from '@/models/vaultDeletion'
import { CommandCopy } from '@/features/vaults/CommandCopy'

// Deleted vaults (#162): restore while pending, purge now, and keep warning about encrypted upstream data whose key
// was never exported until it is (or its key retention ends). Hidden when there is nothing to show.

const stateBadge = (d: VaultDeletion) => {
  if (d.state === 'pending') return <Badge tone="warn" dot>Restorable</Badge>
  if (d.state === 'purging') return d.last_error ? <Badge tone="danger" dot>Purge retrying</Badge> : <Badge tone="info" dot pulse>Purging</Badge>
  return d.key_retained ? <Badge tone="neutral">Purged, key kept</Badge> : <Badge tone="neutral">Purged</Badge>
}

const refresh = () => invalidate('storage.vault.deleted.list', 'storage.vault.list', 'stats.system.storage')

const DeletedRow = ({ d }: { d: VaultDeletion }) => {
  const [busy, setBusy] = useState<null | 'restore' | 'now'>(null)

  const restore = async () => {
    setBusy('restore')
    try {
      await api.send('storage.vault.restore', { id: d.vault_id })
      await refresh()
      notify.success(`“${d.vault_name}” restored`)
    } catch (error) {
      notify.error(error, 'Could not restore the vault')
    } finally {
      setBusy(null)
    }
  }

  const purgeNow = async () => {
    const ok = await confirm({
      title: `Purge “${d.vault_name}” now?`,
      description: `Its data is removed within moments${d.delete_upstream ? ', including the objects in its bucket,' : ''} and it can’t be restored. Its encryption key is still kept until ${formatDateTime(d.key_retain_until)}.`,
      confirmLabel: 'Purge now',
      typeToConfirm: d.vault_name,
    })
    if (!ok) return
    setBusy('now')
    try {
      await api.send('storage.vault.remove', { id: d.vault_id, now: true, confirm_now: true })
      await refresh()
      notify.success(`“${d.vault_name}” is being purged`)
    } catch (error) {
      notify.error(error, isWsError(error, 'needs_confirmation') ? 'Run `vh vault delete --now` from a terminal to answer it' : 'Could not purge the vault')
    } finally {
      setBusy(null)
    }
  }

  return (
    <li className="px-5 py-3.5" data-testid="deleted-vault-row">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div className="min-w-0">
          <div className="flex flex-wrap items-center gap-2">
            <span className="text-fg truncate font-medium">{d.vault_name}</span>
            {stateBadge(d)}
          </div>
          <div className="text-fg-subtle mt-0.5 text-xs">
            {d.type === 's3' ? `S3${d.provider ? ` · ${d.provider}` : ''}${d.bucket ? ` · ${d.bucket}` : ''}` : 'Local disk'}
            {d.owner ? ` · ${d.owner}` : ''} · deleted {formatRelative(d.deleted_at)}
            {d.state === 'pending' ? ` · purged ${formatRelative(d.purge_after)}` : ''}
            {d.key_retained ? ` · key kept until ${formatDateTime(d.key_retain_until)}` : ''}
          </div>
          {d.last_error ? <div className="text-danger mt-1 text-xs">{d.last_error}</div> : null}
        </div>
        {d.state !== 'purged' ?
          <div className="flex shrink-0 gap-2">
            {d.restorable ?
              <Button size="sm" variant="secondary" loading={busy === 'restore'} disabled={busy !== null} onClick={() => void restore()}>
                <ArrowsRotateIcon aria-hidden />
                Restore
              </Button>
            : null}
            <Button size="sm" variant="danger" loading={busy === 'now'} disabled={busy !== null} onClick={() => void purgeNow()}>
              Purge now
            </Button>
          </div>
        : null}
      </div>
      {d.upstream_key_at_risk ?
        <div className="border-danger-line bg-danger-soft mt-3 rounded-control border p-3" role="alert" data-testid="deleted-vault-key-at-risk">
          <div className="text-danger flex items-center gap-2 text-sm font-semibold">
            <TriangleExclamationIcon className="size-4 shrink-0" aria-hidden />
            Export this key before {formatDateTime(d.key_retain_until)}
          </div>
          <p className="text-fg-muted mt-1 text-sm">
            The objects kept in {d.bucket ?? 'its bucket'} are encrypted with this vault’s key, which was never exported. Without it
            that data can never be decrypted.
          </p>
          <CommandCopy command={d.export_command} label="Copy key export command" />
        </div>
      : null}
    </li>
  )
}

export const DeletedVaults = () => {
  const deleted = useWs('storage.vault.deleted.list', null, {
    select: data => data.deleted ?? [],
    refetchInterval: 15_000,
    retry: false,
  })
  const rows = deleted.data ?? []
  // Purged tombstones whose key is gone and nothing to warn about are history, not something to act on.
  const visible = rows.filter(d => d.state !== 'purged' || d.key_retained)
  if (!visible.length) return null

  return (
    <section className="panel mt-6" data-testid="deleted-vaults">
      <div className="border-line border-b px-5 py-3.5">
        <h2 className="text-fg text-[15px] font-semibold">Deleted vaults</h2>
        <p className="text-fg-subtle mt-0.5 text-sm">
          Restorable until their purge starts. Encryption keys are kept for the key retention window and can be exported with{' '}
          <code className="font-mono text-xs">vh vault keys export</code>.
        </p>
      </div>
      <ul className="divide-line divide-y">
        {visible.map(d => (
          <DeletedRow key={d.vault_id} d={d} />
        ))}
      </ul>
    </section>
  )
}

export default DeletedVaults
