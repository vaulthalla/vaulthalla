import React, { useEffect, useMemo, useState } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import { VaultSecurity, VaultSecurityOverallStatus } from '@/models/stats/vaultSecurity'
import { useStatsStore } from '@/stores/statsStore'
import { getErrorMessage } from '@/util/handleErrors'

type VaultSecurityProps = { vaultId: number; intervalMs?: number }
type Tone = { border: string; bg: string; text: string; dot: string; bar: string }

const tones: Record<VaultSecurityOverallStatus, Tone> = {
  healthy: {
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    bar: 'bg-emerald-300/80',
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
}

const cyanTone: Tone = {
  border: 'border-cyan-300/25',
  bg: 'bg-cyan-400/10',
  text: 'text-cyan-100',
  dot: 'bg-cyan-300',
  bar: 'bg-cyan-300/75',
}

function formatInt(n: number): string {
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
  const seconds = Math.max(0, Math.floor((Date.now() - ms) / 1000))
  if (seconds < 60) return `${seconds}s ago`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`
  return `${Math.floor(seconds / 86400)}d ago`
}

function formatPercent(value: number): string {
  return `${(Math.max(0, Number.isFinite(value) ? value : 0) * 100).toFixed(1)}%`
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

export default function VaultSecurityComponent({ vaultId, intervalMs = 7500 }: VaultSecurityProps) {
  const [security, setSecurity] = useState<VaultSecurity | null>(null)
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
        const next = await useStatsStore.getState().getVaultSecurity({ vault_id: vaultId })
        if (!alive) return
        setSecurity(next)
        setLastUpdated(Date.now())
      } catch (err: unknown) {
        if (!alive) return
        setError(getErrorMessage(err) || 'Failed to fetch vault security')
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

  const data = useMemo(() => security ?? new VaultSecurity({ vault_id: vaultId }), [security, vaultId])
  const tone = tones[data.overall_status] ?? tones.unknown
  const encryptionTone = data.encryption_status === 'encrypted' ? tones.healthy : data.encryption_status === 'mixed' ? tones.warning : tones.unknown
  const legacyTone = data.files_legacy_key_version || data.files_unknown_key_version ? tones.warning : tones.healthy
  const deniedTone = data.unauthorized_access_attempts_24h || data.rate_limited_attempts_24h ? tones.warning : tones.healthy
  const integrityTone = data.integrity_check_status === 'failed' ? tones.critical : data.integrity_check_status === 'passed' ? tones.healthy : tones.unknown
  const right = <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />
  const legacyWidth = `${Math.min(100, Math.max(3, data.file_count > 0 ? data.legacyCoverage() * 100 : 3))}%`

  return (
    <StatsCard
      title="Security / Integrity"
      subtitle="Key-version coverage, denied share access, and honest integrity verifier availability."
      right={right}
    >
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Overall" value={data.overall_status} detail="vault posture" tone={tone} />
          <SummaryPill
            label="Encryption"
            value={data.encryption_status}
            detail={data.current_key_version == null ? 'no current key' : `key v${data.current_key_version}`}
            tone={encryptionTone}
          />
          <SummaryPill
            label="Legacy files"
            value={formatInt(data.files_legacy_key_version + data.files_unknown_key_version)}
            detail={`${formatInt(data.files_current_key_version)} current / ${formatInt(data.file_count)} total`}
            tone={legacyTone}
          />
          <SummaryPill
            label="Key age"
            value={data.key_age_days == null ? 'unknown' : `${formatInt(data.key_age_days)}d`}
            detail={`${formatInt(data.trashed_key_versions_count)} old versions`}
            tone={data.key_age_days == null ? tones.unknown : cyanTone}
          />
          <SummaryPill
            label="Denied access"
            value={formatInt(data.unauthorized_access_attempts_24h)}
            detail={`${formatInt(data.rate_limited_attempts_24h)} rate limited`}
            tone={deniedTone}
          />
          <SummaryPill
            label="Integrity"
            value={data.integrity_check_status.replace(/_/g, ' ')}
            detail="verifier state"
            tone={integrityTone}
          />
        </div>

        {error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {error}
          </div>
        ) : null}

        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-3 flex items-center justify-between gap-3">
              <div className="text-xs font-semibold text-white/75 uppercase">Encryption coverage</div>
              <div className="text-xs text-white/35">{formatPercent(data.legacyCoverage())} legacy/unknown</div>
            </div>
            <div className="h-2 overflow-hidden rounded-full bg-white/10">
              <div className={['h-full rounded-full', legacyTone.bar].join(' ')} style={{ width: legacyWidth }} />
            </div>
            <div className="mt-3">
              <DetailRow label="Current key version" value={data.current_key_version == null ? 'unknown' : `v${data.current_key_version}`} />
              <DetailRow label="Key created" value={formatAge(data.key_created_at)} />
              <DetailRow label="Current-key files" value={formatInt(data.files_current_key_version)} />
              <DetailRow label="Legacy-key files" value={formatInt(data.files_legacy_key_version)} tone={legacyTone} />
              <DetailRow label="Unknown-key files" value={formatInt(data.files_unknown_key_version)} tone={legacyTone} />
            </div>
          </div>

          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-3 text-xs font-semibold text-white/75 uppercase">Access and policy changes</div>
            <DetailRow label="Denied/failed 24h" value={formatInt(data.unauthorized_access_attempts_24h)} tone={deniedTone} />
            <DetailRow label="Rate limited 24h" value={formatInt(data.rate_limited_attempts_24h)} tone={deniedTone} />
            <DetailRow label="Last denied access" value={formatAge(data.last_denied_access_at)} />
            <DetailRow label="Last denied reason" value={data.last_denied_access_reason ?? 'none recorded'} />
            <DetailRow label="Last permission change" value={formatAge(data.last_permission_change_at)} />
            <DetailRow label="Last share policy change" value={formatAge(data.last_share_policy_change_at)} />
            <DetailRow label="Checksum mismatches" value={data.checksum_mismatch_count == null ? 'not available' : formatInt(data.checksum_mismatch_count)} tone={integrityTone} />
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
