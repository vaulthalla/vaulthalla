'use client'

import React, { useEffect, useMemo, useState } from 'react'
import dynamic from 'next/dynamic'
import { useWs } from '@/lib/query'
import { PageHeader, Panel } from '@/components/ui/Panel'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { Select } from '@/components/ui/Field'
import { Checkbox } from '@/components/ui/Choice'
import { Tooltip } from '@/components/ui/Tooltip'
import { DataTable, type Column } from '@/components/ui/DataTable'
import { EmptyState, QueryState } from '@/components/ui/State'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { CircleCheckIcon, ArrowRightArrowLeftIcon, XmarkIcon } from '@/components/ui/icons'
import { formatBytes, formatDateTime, formatRelative } from '@/lib/format'
import { useSyncConflictSummary } from '@/features/syncConflicts/summary'
import { NO_OVERWRITE, RESOLUTION_EFFECT, RESOLUTION_LABEL, STATUS_LABEL, resolveConflicts } from '@/features/syncConflicts/model'
import type { SyncConflict, SyncConflictResolution, SyncConflictResolveResponse } from '@/models/syncConflicts'

// The side-by-side preview (and its diff) only loads when someone opens a conflict.
const ConflictSheet = dynamic(() => import('@/features/syncConflicts/ConflictSheet').then(m => m.ConflictSheet), { ssr: false })

interface LastRun {
  response: SyncConflictResolveResponse
  // Paths captured before the list refetches (resolved rows disappear from it).
  paths: Map<number, string>
}

const SideCell = ({ size, modified }: { size: number; modified: string | null }) => (
  <div className="flex flex-col">
    <span className="tabular text-fg">{formatBytes(size)}</span>
    <span className="text-xs text-fg-subtle" title={formatDateTime(modified)}>
      {formatRelative(modified)}
    </span>
  </div>
)

export const SyncConflictsPage = () => {
  const summary = useSyncConflictSummary()
  const [vaultId, setVaultId] = useState<number | null>(null)
  const list = useWs('sync.conflicts.list', vaultId ? { vault_id: vaultId } : {}, { refetchInterval: 60_000 })
  const [selected, setSelected] = useState<Set<number>>(new Set())
  const [busy, setBusy] = useState<Set<number>>(new Set())
  const [bulkBusy, setBulkBusy] = useState<SyncConflictResolution | null>(null)
  const [lastRun, setLastRun] = useState<LastRun | null>(null)
  const [openId, setOpenId] = useState<number | null>(null)

  const vaults = useMemo(() => summary.data?.vaults ?? [], [summary.data])
  const rows = useMemo(() => list.data?.conflicts ?? [], [list.data])
  const resolvable = useMemo(() => rows.filter(row => row.can_overwrite), [rows])
  const open = rows.find(row => row.id === openId) ?? null

  // A vault filter that no longer has conflicts falls back to all vaults.
  useEffect(() => {
    if (vaultId !== null && summary.data && !vaults.some(v => v.vault_id === vaultId)) setVaultId(null)
  }, [vaultId, vaults, summary.data])

  // Selection only ever holds rows that are still listed and resolvable.
  useEffect(() => {
    setSelected(current => {
      const keep = new Set([...current].filter(id => resolvable.some(row => row.id === id)))
      return keep.size === current.size ? current : keep
    })
  }, [resolvable])

  const run = async (resolution: SyncConflictResolution, targets: SyncConflict[]) => {
    const ids = targets.map(t => t.id)
    const paths = new Map(targets.map(t => [t.id, `${t.vault_name}: ${t.path}`]))
    setBusy(current => new Set([...current, ...ids]))
    try {
      const response = await resolveConflicts(resolution, ids)
      setSelected(current => new Set([...current].filter(id => !response.results.some(r => r.ok && r.conflict_id === id))))
      return { response, paths }
    } finally {
      setBusy(current => new Set([...current].filter(id => !ids.includes(id))))
    }
  }

  const resolveOne = async (conflict: SyncConflict, resolution: SyncConflictResolution) => {
    try {
      const { response } = await run(resolution, [conflict])
      const result = response.results.find(r => r.conflict_id === conflict.id)
      if (result?.ok) {
        notify.success(`${RESOLUTION_LABEL[resolution]}: ${conflict.name}`, RESOLUTION_EFFECT[resolution])
        if (openId === conflict.id) setOpenId(null)
        return true
      }
      notify.error(result?.message ?? `${conflict.name}: ${STATUS_LABEL[result?.status ?? 'error']}`)
    } catch (error) {
      notify.error(error, 'The conflict could not be resolved')
    }
    return false
  }

  const resolveSelected = async (resolution: SyncConflictResolution) => {
    const targets = resolvable.filter(row => selected.has(row.id))
    if (targets.length === 0) return
    const ok = await confirm({
      title: `${RESOLUTION_LABEL[resolution]} for ${targets.length} file${targets.length === 1 ? '' : 's'}?`,
      description: (
        <>
          {RESOLUTION_EFFECT[resolution]} Each file is handled on its own: one that changed since it was recorded is
          refused and stays in the list. Transfers count against the vault’s S3 request and price budgets.
        </>
      ),
      confirmLabel: RESOLUTION_LABEL[resolution],
      tone: 'primary',
    })
    if (!ok) return
    setBulkBusy(resolution)
    try {
      const result = await run(resolution, targets)
      setLastRun(result)
      if (result.response.failed === 0) notify.success(`${result.response.resolved} conflict${result.response.resolved === 1 ? '' : 's'} resolved`)
    } catch (error) {
      notify.error(error, 'The conflicts could not be resolved')
    } finally {
      setBulkBusy(null)
    }
  }

  const allChecked = resolvable.length > 0 && resolvable.every(row => selected.has(row.id))
  const someChecked = resolvable.some(row => selected.has(row.id))

  const actionButtons = (row: SyncConflict) => {
    const disabled = !row.can_overwrite || busy.has(row.id)
    const buttons = (
      <div className="flex justify-end gap-1.5">
        {(['keep_local', 'keep_remote'] as const).map(resolution => (
          <Button
            key={resolution}
            size="sm"
            variant="secondary"
            disabled={disabled}
            loading={busy.has(row.id)}
            onClick={() => void resolveOne(row, resolution)}
            data-testid={`conflict-${resolution}-${row.id}`}>
            {RESOLUTION_LABEL[resolution]}
          </Button>
        ))}
      </div>
    )
    return row.can_overwrite ? buttons : <Tooltip content={NO_OVERWRITE} side="left">{buttons}</Tooltip>
  }

  const columns: Column<SyncConflict>[] = [
    {
      key: 'select',
      header: (
        <Checkbox
          aria-label="Select all resolvable conflicts"
          checked={allChecked ? true : someChecked ? 'indeterminate' : false}
          disabled={resolvable.length === 0}
          onCheckedChange={checked => setSelected(checked ? new Set(resolvable.map(row => row.id)) : new Set())}
        />
      ),
      headerClassName: 'w-10',
      className: 'w-10',
      cell: row => (
        <Checkbox
          aria-label={`Select ${row.path}`}
          checked={selected.has(row.id)}
          disabled={!row.can_overwrite}
          title={row.can_overwrite ? undefined : NO_OVERWRITE}
          onCheckedChange={checked =>
            setSelected(current => {
              const next = new Set(current)
              if (checked) next.add(row.id)
              else next.delete(row.id)
              return next
            })
          }
        />
      ),
    },
    {
      key: 'file',
      header: 'File',
      sortValue: row => row.path,
      cell: row => (
        <div className="flex min-w-0 flex-col">
          <span className="truncate font-medium text-fg">{row.name}</span>
          <span className="truncate text-xs text-fg-subtle" title={row.path}>
            {row.path}
          </span>
        </div>
      ),
    },
    { key: 'vault', header: 'Vault', sortValue: row => row.vault_name, hideBelow: 'md', cell: row => row.vault_name },
    {
      key: 'local',
      header: 'Local',
      hideBelow: 'lg',
      sortValue: row => row.local.modified_at,
      cell: row => <SideCell size={row.local.size_bytes} modified={row.local.modified_at} />,
    },
    {
      key: 'remote',
      header: 'Remote',
      hideBelow: 'lg',
      sortValue: row => row.remote.modified_at,
      cell: row => <SideCell size={row.remote.size_bytes} modified={row.remote.modified_at} />,
    },
    {
      key: 'detected',
      header: 'Detected',
      hideBelow: 'sm',
      sortValue: row => row.created_at,
      cell: row => (
        <span className="text-fg-muted" title={formatDateTime(row.created_at)}>
          {formatRelative(row.created_at)}
        </span>
      ),
    },
    { key: 'actions', header: <span className="sr-only">Resolve</span>, headerClassName: 'text-right', cell: row => actionButtons(row) },
  ]

  const toolbar = (
    <>
      {vaults.length > 1 ? (
        <Select
          aria-label="Filter by vault"
          className="h-8 w-auto min-w-40"
          value={vaultId ?? ''}
          onChange={event => setVaultId(event.target.value ? Number(event.target.value) : null)}>
          <option value="">All vaults ({summary.data?.total ?? 0})</option>
          {vaults.map(v => (
            <option key={v.vault_id} value={v.vault_id}>
              {v.vault_name} ({v.count})
            </option>
          ))}
        </Select>
      ) : null}
      <span className="text-xs text-fg-subtle tabular">{selected.size ? `${selected.size} selected` : null}</span>
      <Button size="sm" variant="secondary" disabled={!selected.size || bulkBusy !== null} loading={bulkBusy === 'keep_local'} onClick={() => void resolveSelected('keep_local')}>
        Keep local
      </Button>
      <Button size="sm" variant="secondary" disabled={!selected.size || bulkBusy !== null} loading={bulkBusy === 'keep_remote'} onClick={() => void resolveSelected('keep_remote')}>
        Keep remote
      </Button>
    </>
  )

  const failures = lastRun ? lastRun.response.results.filter(r => !r.ok) : []

  return (
    <>
      <PageHeader
        eyebrow="System"
        title="Sync Conflicts"
        description="Files that changed both in the vault and in its S3 bucket since they were last in sync, in vaults where the conflict policy is Ask. Each one waits for a decision; everything else keeps syncing."
      />

      {lastRun ? (
        <Panel
          className="mb-4"
          title={`${RESOLUTION_LABEL[lastRun.response.resolution]}: ${lastRun.response.resolved} resolved, ${lastRun.response.failed} not resolved`}
          actions={<Button size="sm" variant="ghost" onClick={() => setLastRun(null)} aria-label="Dismiss results"><XmarkIcon aria-hidden /></Button>}>
          {failures.length ? (
            <ul className="space-y-1.5 text-sm" data-testid="conflict-bulk-failures">
              {failures.map(result => (
                <li key={result.conflict_id} className="flex flex-wrap items-baseline gap-2">
                  <Badge tone={result.status === 'conflict' ? 'warn' : 'danger'}>{STATUS_LABEL[result.status] ?? result.status}</Badge>
                  <span className="text-fg">{lastRun.paths.get(result.conflict_id) ?? `#${result.conflict_id}`}</span>
                  {result.message ? <span className="text-fg-subtle">{result.message}</span> : null}
                </li>
              ))}
            </ul>
          ) : (
            <p className="flex items-center gap-2 text-sm text-fg-muted">
              <CircleCheckIcon className="size-4 text-ok" aria-hidden /> Every selected conflict was resolved.
            </p>
          )}
        </Panel>
      ) : null}

      <QueryState query={list}>
        {() => (
          <DataTable
            rows={rows}
            columns={columns}
            rowKey={row => row.id}
            onRowClick={row => setOpenId(row.id)}
            toolbar={rows.length ? toolbar : undefined}
            filter={(row, q) => row.path.toLowerCase().includes(q) || row.vault_name.toLowerCase().includes(q)}
            filterPlaceholder="Filter by path or vault…"
            initialSort={{ key: 'detected', dir: 'desc' }}
            empty={
              <EmptyState
                icon={ArrowRightArrowLeftIcon}
                title="No sync conflicts"
                description="Nothing is waiting for a decision in the vaults you can resolve conflicts in."
              />
            }
          />
        )}
      </QueryState>

      {open ? (
        <ConflictSheet
          conflict={open}
          busy={busy.has(open.id)}
          onClose={() => setOpenId(null)}
          onResolve={resolution => resolveOne(open, resolution)}
        />
      ) : null}
    </>
  )
}
