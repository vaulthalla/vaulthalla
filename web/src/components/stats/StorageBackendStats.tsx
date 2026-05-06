'use client'

import React, { useEffect } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type {
  StorageBackendStats,
  StorageBackendStatus,
  StorageVaultBackendStats,
} from '@/models/stats/storageBackendStats'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones: Record<StorageBackendStatus, Tone> = {
  healthy: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    bar: 'bg-emerald-300/80',
  },
  degraded: {
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    bar: 'bg-amber-300/80',
  },
  error: {
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
    bar: 'bg-rose-300/80',
  },
  unknown: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/65',
    dot: 'bg-white/35',
    bar: 'bg-white/35',
  },
}

const cyanTone: Tone = {
  border: 'border-cyan-300/25',
  bg: 'bg-cyan-400/10',
  text: 'text-cyan-100',
  dot: 'bg-cyan-300',
  bar: 'bg-cyan-300/75',
}

function formatInt(n: number | null): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function formatBytes(bytes: number | null): string {
  if (bytes == null || !Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / Math.pow(1024, i)
  return `${value >= 10 ? value.toFixed(0) : value.toFixed(1)} ${units[i]}`
}

function formatCheckedAt(value: number | string | null, fallback: number | null): string {
  if (typeof value === 'number' && Number.isFinite(value) && value > 0) {
    const ms = value > 1_000_000_000_000 ? value : value * 1000
    return new Date(ms).toLocaleTimeString()
  }
  if (typeof value === 'string') {
    const parsed = Date.parse(value)
    if (Number.isFinite(parsed)) return new Date(parsed).toLocaleTimeString()
  }
  return fallback ? new Date(fallback).toLocaleTimeString() : 'pending'
}

const LiveBadge = ({
  loading,
  error,
  lastUpdated,
}: {
  loading: boolean
  error: string | null
  lastUpdated: number | null
}) => (
  <div className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-3 py-1 text-xs text-white/70 backdrop-blur">
    <span
      className={[
        'h-1.5 w-1.5 rounded-full',
        error ? 'bg-rose-400/80'
        : loading ? 'bg-amber-300/80'
        : 'bg-emerald-400/80',
      ].join(' ')}
    />
    {error ? 'error' : loading ? 'updating...' : 'live'}
    {lastUpdated ? <span className="text-white/35">/</span> : null}
    {lastUpdated ? <span className="text-white/50">{new Date(lastUpdated).toLocaleTimeString()}</span> : null}
  </div>
)

const SummaryPill = ({
  label,
  value,
  detail,
  tone,
}: {
  label: string
  value: React.ReactNode
  detail?: React.ReactNode
  tone: Tone
}) => (
  <div className={['rounded-2xl border px-3 py-3 backdrop-blur', tone.border, tone.bg].join(' ')}>
    <div className="flex items-center justify-between gap-3">
      <div className="text-[11px] text-white/50 uppercase">{label}</div>
      <span className={['h-1.5 w-1.5 rounded-full', tone.dot].join(' ')} />
    </div>
    <div className={['mt-2 truncate text-lg font-semibold', tone.text].join(' ')}>{value}</div>
    {detail ? <div className="mt-1 truncate text-xs text-white/45">{detail}</div> : null}
  </div>
)

const Metric = ({ label, value, tone }: { label: string; value: React.ReactNode; tone?: Tone }) => (
  <div className="min-w-0">
    <div className="text-[11px] text-white/40 uppercase">{label}</div>
    <div className={['mt-1 truncate text-sm font-semibold', tone?.text ?? 'text-white/80'].join(' ')}>{value}</div>
  </div>
)

const StatusBadge = ({ status }: { status: StorageBackendStatus }) => {
  const tone = tones[status] ?? tones.unknown
  return (
    <span className={['inline-flex items-center gap-1.5 rounded-full border px-2 py-0.5 text-xs', tone.border, tone.bg, tone.text].join(' ')}>
      <span className={['h-1.5 w-1.5 rounded-full', tone.dot].join(' ')} />
      {status}
    </span>
  )
}

const VaultRow = ({ vault }: { vault: StorageVaultBackendStats }) => {
  const tone = tones[vault.backend_status] ?? tones.unknown
  const freeTone =
    vault.min_free_space_ok === false ? tones.degraded
    : vault.min_free_space_ok === true ? tones.healthy
    : tones.unknown
  const totalKnown = vault.knownUsageBytes() + Math.max(0, vault.free_space_bytes ?? 0)
  const usedWidth = `${Math.min(100, Math.max(3, totalKnown > 0 ? (vault.knownUsageBytes() / totalKnown) * 100 : 3))}%`
  const quotaLabel = vault.quota_bytes > 0 ? formatBytes(vault.quota_bytes) : 'not set'
  const providerText =
    vault.hasProviderInstrumentation() ?
      `${formatInt(vault.provider_errors_total ?? 0)} errors / ${formatInt(vault.provider_ops_total ?? 0)} ops`
    : 'not instrumented'

  return (
    <div className={['rounded-2xl border bg-white/5 p-3 backdrop-blur', tone.border].join(' ')}>
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div className="min-w-0">
          <div className="truncate text-sm font-semibold text-white/90">{vault.vault_name}</div>
          <div className="mt-1 text-xs text-white/45">
            vault #{formatInt(vault.vault_id)} / {vault.type}
          </div>
        </div>
        <StatusBadge status={vault.backend_status} />
      </div>

      <div className="mt-3 grid grid-cols-2 gap-3 md:grid-cols-4">
        <Metric label="Vault data" value={formatBytes(vault.vault_size_bytes)} />
        <Metric label="Cache" value={formatBytes(vault.cache_size_bytes)} />
        <Metric label="Free" value={formatBytes(vault.free_space_bytes)} tone={freeTone} />
        <Metric label="Quota" value={quotaLabel} />
      </div>

      <div className="mt-3 h-1.5 overflow-hidden rounded-full bg-white/10">
        <div className={['h-full rounded-full', tone.bar].join(' ')} style={{ width: usedWidth }} />
      </div>

      <div className="mt-3 grid grid-cols-2 gap-3 md:grid-cols-4">
        <Metric label="Active" value={vault.active ? 'yes' : 'no'} tone={vault.active ? tones.healthy : tones.degraded} />
        <Metric label="FS write" value={vault.allow_fs_write ? 'allowed' : 'blocked'} tone={vault.allow_fs_write ? cyanTone : tones.degraded} />
        <Metric
          label="S3 encryption"
          value={vault.type === 's3' ? (vault.upstream_encryption_enabled ? 'enabled' : 'disabled') : 'n/a'}
          tone={vault.type === 's3' ? (vault.upstream_encryption_enabled ? tones.healthy : tones.degraded) : tones.unknown}
        />
        <Metric label="Provider ops" value={providerText} tone={vault.hasProviderInstrumentation() ? cyanTone : tones.unknown} />
      </div>

      {vault.type === 's3' ? (
        <div className="mt-3 rounded-xl border border-white/10 bg-black/10 px-3 py-2 text-xs text-white/50">
          Bucket: <span className="font-semibold text-white/75">{vault.bucket ?? 'unknown'}</span>
        </div>
      ) : null}

      {vault.last_provider_error ? (
        <div className="mt-3 rounded-xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-xs text-rose-100">
          {vault.last_provider_error}
        </div>
      ) : null}
    </div>
  )
}

export function StorageBackendBody({
  stats,
  lastUpdated,
}: {
  stats: StorageBackendStats
  lastUpdated: number | null
}) {
  const tone = tones[stats.overall_status] ?? tones.unknown
  const checkedAt = formatCheckedAt(stats.checked_at, lastUpdated)
  const problemTone = stats.problemVaultCount() > 0 ? tones.degraded : tones.healthy

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
        <SummaryPill label="Status" value={stats.overall_status} detail={`checked ${checkedAt}`} tone={tone} />
        <SummaryPill label="Vaults" value={formatInt(stats.vault_count_total)} detail={`${formatInt(stats.active_vault_count)} active`} tone={cyanTone} />
        <SummaryPill label="Local" value={formatInt(stats.local_vault_count)} detail="disk-backed" tone={cyanTone} />
        <SummaryPill label="S3" value={formatInt(stats.s3_vault_count)} detail="object-backed" tone={cyanTone} />
        <SummaryPill label="Problems" value={formatInt(stats.problemVaultCount())} detail={`${formatInt(stats.error_vault_count)} error`} tone={problemTone} />
        <SummaryPill label="Free known" value={formatBytes(stats.totalFreeSpaceBytes())} detail="reported by engines" tone={cyanTone} />
      </div>

      <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
        {stats.vaults.length ?
          stats.vaults.map(vault => <VaultRow key={vault.vault_id || vault.vault_name} vault={vault} />)
        : <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-4 text-sm text-white/50">
            No storage engines are available yet.
          </div>
        }
      </div>
    </div>
  )
}

export default function StorageBackendStatsComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.storageBackendStats)
  const startPolling = useStatsStore(s => s.startStorageBackendStatsPolling)
  const stopPolling = useStatsStore(s => s.stopStorageBackendStatsPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="Storage Backend"
      subtitle="Local and S3 vault provider status, free-space signals, and configuration readiness"
      right={right}
      className="w-full"
    >
      <div className="space-y-4">
        {wrapper.error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        ) : null}
        <StorageBackendBody stats={wrapper.data} lastUpdated={wrapper.lastUpdated} />
      </div>
    </StatsCard>
  )
}
