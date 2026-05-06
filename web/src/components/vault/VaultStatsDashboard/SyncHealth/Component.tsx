import React, { useEffect, useMemo, useState } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { IEvent } from '@/models/stats/sync'
import { VaultSyncHealth, type VaultSyncOverallStatus } from '@/models/stats/vaultSyncHealth'
import { useStatsStore } from '@/stores/statsStore'
import { getErrorMessage } from '@/util/handleErrors'

type SyncHealthProps = { vaultId: number; initialLatestEvent?: IEvent | null; intervalMs?: number }

type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones = {
  healthy: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    bar: 'bg-emerald-300/80',
  },
  syncing: {
    border: 'border-cyan-300/30',
    bg: 'bg-cyan-400/10',
    text: 'text-cyan-100',
    dot: 'bg-cyan-300',
    bar: 'bg-cyan-300/75',
  },
  warning: {
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    bar: 'bg-amber-300/80',
  },
  critical: {
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
} satisfies Record<string, Tone>

function toneForStatus(status: VaultSyncOverallStatus): Tone {
  if (status === 'healthy') return tones.healthy
  if (status === 'syncing') return tones.syncing
  if (status === 'warning') return tones.warning
  if (status === 'stalled' || status === 'error' || status === 'diverged') return tones.critical
  return tones.unknown
}

function formatInt(n: number): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB']
  const i = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / Math.pow(1024, i)
  return `${value >= 10 ? value.toFixed(0) : value.toFixed(1)} ${units[i]}`
}

function formatRate(bytesPerSec: number): string {
  return `${formatBytes(bytesPerSec)}/s`
}

function formatAge(seconds: number | null | undefined): string {
  if (seconds == null || !Number.isFinite(seconds)) return 'unknown'
  const s = Math.max(0, seconds)
  if (s < 60) return `${Math.round(s)}s ago`
  if (s < 3600) return `${Math.round(s / 60)}m ago`
  if (s < 86400) return `${Math.round(s / 3600)}h ago`
  return `${Math.round(s / 86400)}d ago`
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
    {error ?
      'error'
    : loading ?
      'updating...'
    : 'live'}
    {lastUpdated ?
      <span className="text-white/35">/</span>
    : null}
    {lastUpdated ?
      <span className="text-white/50">{new Date(lastUpdated).toLocaleTimeString()}</span>
    : null}
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
    {detail ?
      <div className="mt-1 truncate text-xs text-white/45">{detail}</div>
    : null}
  </div>
)

const DetailMetric = ({
  label,
  value,
  detail,
  tone,
}: {
  label: string
  value: React.ReactNode
  detail?: React.ReactNode
  tone?: Tone
}) => (
  <div className="rounded-2xl border border-white/10 bg-black/10 px-3 py-2">
    <div className="text-[11px] text-white/45 uppercase">{label}</div>
    <div className={['mt-1 truncate text-sm font-semibold', tone?.text ?? 'text-white/85'].join(' ')}>{value}</div>
    {detail ?
      <div className="mt-1 truncate text-xs text-white/40">{detail}</div>
    : null}
  </div>
)

export default function SyncHealth({ vaultId, initialLatestEvent = null, intervalMs = 7500 }: SyncHealthProps) {
  const [health, setHealth] = useState<VaultSyncHealth | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [lastUpdated, setLastUpdated] = useState<number | null>(null)

  useEffect(() => {
    let alive = true
    let inflight = false

    const refresh = async () => {
      if (inflight) return
      inflight = true
      setLoading(true)
      setError(null)

      try {
        const next = await useStatsStore.getState().getVaultSyncHealth({ vault_id: vaultId })
        if (!alive) return
        setHealth(next)
        setLastUpdated(Date.now())
      } catch (err: unknown) {
        if (!alive) return
        setError(getErrorMessage(err) || 'Failed to fetch vault sync health')
      } finally {
        inflight = false
        if (alive) setLoading(false)
      }
    }

    void refresh()
    const id = window.setInterval(() => void refresh(), intervalMs)

    return () => {
      alive = false
      window.clearInterval(id)
    }
  }, [vaultId, intervalMs])

  const data = useMemo(() => health ?? new VaultSyncHealth({ vault_id: vaultId }), [health, vaultId])
  const tone = toneForStatus(data.overall_status)
  const latest = health?.latest_event ?? initialLatestEvent ?? null

  const subtitle = useMemo(() => {
    const parts = [
      `state ${data.current_state}`,
      data.sync_enabled ? `interval ${formatInt(data.sync_interval_seconds)}s` : 'sync disabled',
      data.conflict_policy ? `policy ${data.conflict_policy}` : null,
      data.configured_strategy ? `strategy ${data.configured_strategy}` : null,
    ].filter(Boolean)
    return parts.join(' / ')
  }, [data])

  const checkedAt = formatCheckedAt(data.checked_at, lastUpdated)
  const right = <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />
  const latestStatus = latest?.status ?? 'none'
  const latestTrigger = latest?.trigger ?? 'none'
  const latestRetry = latest?.retry_attempt ?? 0
  const problemTone = data.hasProblems() ? tones.critical : tones.healthy

  return (
    <StatsCard title="Sync Health" subtitle={subtitle} right={right}>
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Overall" value={data.overall_status} detail={`checked ${checkedAt}`} tone={tone} />
          <SummaryPill
            label="State"
            value={data.current_state}
            detail={data.isActive() ? 'active' : 'not active'}
            tone={tone}
          />
          <SummaryPill
            label="Last success"
            value={formatAge(data.last_success_age_seconds)}
            detail={data.last_success_at ? 'completed sync' : 'no success recorded'}
            tone={data.last_success_at ? tones.healthy : tones.unknown}
          />
          <SummaryPill
            label="Active"
            value={formatInt(data.active_run_count)}
            detail={`oldest ${formatAge(data.oldest_active_age_seconds)}`}
            tone={data.active_run_count > 0 ? tones.syncing : tones.unknown}
          />
          <SummaryPill
            label="Errors"
            value={`${formatInt(data.error_count_24h)} / ${formatInt(data.error_count_7d)}`}
            detail="24h / 7d"
            tone={data.error_count_24h || data.error_count_7d ? tones.critical : tones.healthy}
          />
          <SummaryPill
            label="Conflicts"
            value={formatInt(data.conflict_count_open)}
            detail={`${formatInt(data.conflict_count_24h)} in 24h`}
            tone={data.conflict_count_open > 0 ? tones.warning : tones.healthy}
          />
        </div>

        {error ?
          <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs text-rose-200/90">
            {error}
          </div>
        : null}

        <div className="grid grid-cols-1 gap-3 lg:grid-cols-3">
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur lg:col-span-2">
            <div className="mb-3 flex items-center justify-between gap-3">
              <div className="text-xs font-semibold text-white/70 uppercase">Run summary</div>
              <div className={['rounded-full border px-2.5 py-1 text-xs', tone.border, tone.bg, tone.text].join(' ')}>
                {data.overall_status}
              </div>
            </div>

            <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-4">
              <DetailMetric label="Latest" value={latestStatus} detail={`trigger ${latestTrigger}`} tone={tone} />
              <DetailMetric
                label="Retry"
                value={formatInt(latestRetry)}
                detail="latest event"
                tone={latestRetry ? tones.warning : undefined}
              />
              <DetailMetric
                label="Heartbeat"
                value={formatAge(data.latest_heartbeat_age_seconds)}
                detail="latest active"
                tone={
                  data.latest_heartbeat_age_seconds && data.latest_heartbeat_age_seconds > 120 ?
                    tones.critical
                  : undefined
                }
              />
              <DetailMetric
                label="Divergence"
                value={data.divergence_detected || data.hash_mismatch ? 'detected' : 'none'}
                detail={data.hash_mismatch ? 'hash mismatch' : 'state hashes'}
                tone={data.divergence_detected || data.hash_mismatch ? tones.critical : tones.healthy}
              />
              <DetailMetric
                label="Failed ops 24h"
                value={formatInt(data.failed_ops_24h)}
                tone={data.failed_ops_24h ? tones.critical : undefined}
              />
              <DetailMetric
                label="Failed ops 7d"
                value={formatInt(data.failed_ops_7d)}
                tone={data.failed_ops_7d ? tones.warning : undefined}
              />
              <DetailMetric
                label="Retries 24h"
                value={formatInt(data.retry_count_24h)}
                tone={data.retry_count_24h ? tones.warning : undefined}
              />
              <DetailMetric label="Retries 7d" value={formatInt(data.retry_count_7d)} />
            </div>

            {data.last_error_message || data.last_error_code || data.last_stall_reason ?
              <div className="mt-3 rounded-2xl border border-white/10 bg-black/10 px-3 py-2 text-sm text-white/65">
                {data.last_stall_reason ?
                  <div>
                    <span className="font-semibold text-white/80">Stall:</span> {data.last_stall_reason}
                  </div>
                : null}
                {data.last_error_code ?
                  <div>
                    <span className="font-semibold text-white/80">Error code:</span> {data.last_error_code}
                  </div>
                : null}
                {data.last_error_message ?
                  <div>
                    <span className="font-semibold text-white/80">Message:</span> {data.last_error_message}
                  </div>
                : null}
              </div>
            : null}
          </div>

          <div className="space-y-3">
            <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
              <div className="mb-3 text-xs font-semibold text-white/70 uppercase">Traffic</div>
              <div className="grid grid-cols-1 gap-2">
                <DetailMetric
                  label="24h total"
                  value={formatBytes(data.traffic24h())}
                  detail={`up ${formatBytes(data.bytes_up_24h)} / down ${formatBytes(data.bytes_down_24h)}`}
                />
                <DetailMetric
                  label="7d total"
                  value={formatBytes(data.traffic7d())}
                  detail={`up ${formatBytes(data.bytes_up_7d)} / down ${formatBytes(data.bytes_down_7d)}`}
                />
                <DetailMetric label="Avg 24h" value={formatRate(data.avg_throughput_bytes_per_sec_24h)} />
                <DetailMetric label="Peak 24h" value={formatRate(data.peak_throughput_bytes_per_sec_24h)} />
              </div>
            </div>

            <div className={['rounded-2xl border p-3 backdrop-blur', problemTone.border, problemTone.bg].join(' ')}>
              <div className="text-xs font-semibold text-white/70 uppercase">Open risks</div>
              <div className="mt-2 grid grid-cols-2 gap-2">
                <DetailMetric
                  label="Open conflicts"
                  value={formatInt(data.conflict_count_open)}
                  tone={data.conflict_count_open ? tones.warning : tones.healthy}
                />
                <DetailMetric label="7d conflicts" value={formatInt(data.conflict_count_7d)} />
                <DetailMetric label="Pending" value={formatInt(data.pending_run_count)} />
                <DetailMetric
                  label="Stalled"
                  value={formatInt(data.stalled_run_count)}
                  tone={data.stalled_run_count ? tones.critical : undefined}
                />
              </div>
            </div>
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
