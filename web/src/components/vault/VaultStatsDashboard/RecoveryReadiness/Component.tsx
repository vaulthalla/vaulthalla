import React, { useEffect, useMemo, useState } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import { VaultRecovery, VaultRecoveryReadiness } from '@/models/stats/vaultRecovery'
import { useStatsStore } from '@/stores/statsStore'
import { getErrorMessage } from '@/util/handleErrors'

type RecoveryReadinessProps = { vaultId: number; intervalMs?: number }
type Tone = { border: string; bg: string; text: string; dot: string }

const tones: Record<VaultRecoveryReadiness, Tone> = {
  ready: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
  },
  stale: {
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
  },
  failing: {
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
  },
  disabled: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/60',
    dot: 'bg-white/35',
  },
  unknown: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/60',
    dot: 'bg-white/35',
  },
}

const cyanTone: Tone = {
  border: 'border-cyan-300/25',
  bg: 'bg-cyan-400/10',
  text: 'text-cyan-100',
  dot: 'bg-cyan-300',
}

function formatInt(n: number | null): string {
  return new Intl.NumberFormat().format(Math.max(0, Math.trunc(n ?? 0)))
}

function toMs(value: number | string | null): number | null {
  if (typeof value === 'number') {
    if (!Number.isFinite(value) || value <= 0) return null
    return value > 1_000_000_000_000 ? value : value * 1000
  }
  if (typeof value === 'string') {
    const parsed = Date.parse(value)
    return Number.isFinite(parsed) ? parsed : null
  }
  return null
}

function formatAge(value: number | string | null): string {
  const ms = toMs(value)
  if (ms == null) return 'unknown'
  return formatDuration(Math.max(0, Math.floor((Date.now() - ms) / 1000))) + ' ago'
}

function formatRelative(value: number | string | null): string {
  const ms = toMs(value)
  if (ms == null) return 'unknown'
  const deltaSeconds = Math.floor((ms - Date.now()) / 1000)
  return deltaSeconds >= 0 ? `in ${formatDuration(deltaSeconds)}` : `${formatDuration(Math.abs(deltaSeconds))} ago`
}

function formatDuration(seconds: number | null): string {
  if (seconds == null || !Number.isFinite(seconds)) return 'unknown'
  const safe = Math.max(0, Math.floor(seconds))
  if (safe < 60) return `${safe}s`
  if (safe < 3600) return `${Math.floor(safe / 60)}m`
  if (safe < 86400) return `${Math.floor(safe / 3600)}h`
  return `${Math.floor(safe / 86400)}d`
}

function formatDate(value: number | string | null): string {
  const ms = toMs(value)
  return ms == null ? 'unknown' : new Date(ms).toLocaleString()
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

const DetailRow = ({ label, value, tone }: { label: string; value: React.ReactNode; tone?: Tone }) => (
  <div className="flex items-center justify-between gap-3 border-t border-white/10 px-1 py-2 text-xs">
    <span className="text-white/45">{label}</span>
    <span className={['min-w-0 truncate text-right font-semibold', tone?.text ?? 'text-white/75'].join(' ')}>{value}</span>
  </div>
)

export default function RecoveryReadinessComponent({ vaultId, intervalMs = 7500 }: RecoveryReadinessProps) {
  const [recovery, setRecovery] = useState<VaultRecovery | null>(null)
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
        const next = await useStatsStore.getState().getVaultRecovery({ vault_id: vaultId })
        if (!alive) return
        setRecovery(next)
        setLastUpdated(Date.now())
      } catch (err: unknown) {
        if (!alive) return
        setError(getErrorMessage(err) || 'Failed to fetch recovery readiness')
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

  const data = useMemo(() => recovery ?? new VaultRecovery({ vault_id: vaultId }), [recovery, vaultId])
  const readinessTone = tones[data.recovery_readiness] ?? tones.unknown
  const backupTone = data.backup_status === 'error' ? tones.failing
    : data.backup_status === 'disabled' ? tones.disabled
    : data.backup_status === 'syncing' ? cyanTone
    : data.backup_status === 'idle' ? tones.ready
    : tones.unknown
  const staleTone = data.backup_stale == null ? tones.unknown : data.backup_stale ? tones.stale : tones.ready
  const right = <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />

  return (
    <StatsCard
      title="Recovery Readiness"
      subtitle="Backup policy freshness and last verified good state for this vault."
      right={right}
    >
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill
            label="Readiness"
            value={data.recovery_readiness}
            detail={data.backup_policy_present ? 'backup policy' : 'no policy row'}
            tone={readinessTone}
          />
          <SummaryPill
            label="Backup"
            value={data.backup_status}
            detail={data.backup_enabled ? 'enabled' : 'disabled'}
            tone={backupTone}
          />
          <SummaryPill
            label="Last success"
            value={formatAge(data.last_success_at)}
            detail="verified good state"
            tone={data.last_success_at ? cyanTone : tones.unknown}
          />
          <SummaryPill
            label="Next expected"
            value={formatRelative(data.next_expected_backup_at)}
            detail={formatDate(data.next_expected_backup_at)}
            tone={staleTone}
          />
          <SummaryPill
            label="Missed"
            value={formatInt(data.missed_backup_count_estimate)}
            detail={data.backup_stale ? 'estimated backups' : 'on schedule'}
            tone={staleTone}
          />
          <SummaryPill
            label="Retries"
            value={formatInt(data.retry_count)}
            detail="policy retry count"
            tone={data.retry_count > 0 ? tones.stale : cyanTone}
          />
        </div>

        {error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {error}
          </div>
        ) : null}

        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Backup schedule</div>
            <DetailRow label="Interval" value={formatDuration(data.backup_interval_seconds)} />
            <DetailRow label="Retention" value={formatDuration(data.retention_seconds)} />
            <DetailRow label="Last backup attempt" value={formatAge(data.last_backup_at)} />
            <DetailRow label="Last successful backup" value={formatAge(data.last_success_at)} tone={data.last_success_at ? cyanTone : tones.unknown} />
            <DetailRow label="Next expected backup" value={formatDate(data.next_expected_backup_at)} tone={staleTone} />
          </div>

          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Recoverability signals</div>
            <DetailRow label="Time since verified good state" value={formatDuration(data.time_since_last_verified_good_state)} tone={readinessTone} />
            <DetailRow label="Backup stale" value={data.backup_stale == null ? 'unknown' : data.backup_stale ? 'yes' : 'no'} tone={staleTone} />
            <DetailRow label="Missed backup estimate" value={formatInt(data.missed_backup_count_estimate)} tone={staleTone} />
            <DetailRow label="Retry count" value={formatInt(data.retry_count)} />
            <DetailRow label="Last error" value={data.last_error ?? 'none recorded'} tone={data.last_error ? tones.failing : cyanTone} />
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
