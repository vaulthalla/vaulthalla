import React, { useEffect, useMemo, useState } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import { VaultShareRecentEvent, VaultShareStats } from '@/models/stats/vaultShareStats'
import { useStatsStore } from '@/stores/statsStore'
import { getErrorMessage } from '@/util/handleErrors'

type ShareStatsProps = { vaultId: number; intervalMs?: number }
type Tone = { border: string; bg: string; text: string; dot: string }

const tones = {
  healthy: {
    border: 'border-cyan-300/30',
    bg: 'bg-cyan-400/10',
    text: 'text-cyan-100',
    dot: 'bg-cyan-300',
  },
  quiet: {
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/70',
    dot: 'bg-white/35',
  },
  warn: {
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
  },
  danger: {
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
  },
} satisfies Record<string, Tone>

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
  if (ms == null) return 'never'
  const seconds = Math.max(0, Math.floor((Date.now() - ms) / 1000))
  if (seconds < 60) return `${seconds}s ago`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`
  return `${Math.floor(seconds / 86400)}d ago`
}

function eventLabel(type: string): string {
  return type.replace(/^share\./, '').replace(/\./g, ' ')
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

const EventRow = ({ event }: { event: VaultShareRecentEvent }) => (
  <div className="grid grid-cols-1 gap-2 border-t border-white/10 px-1 py-2 text-xs text-white/70 sm:grid-cols-[1fr_90px_120px_100px]">
    <div className="min-w-0">
      <div className="truncate font-semibold text-white/85">{eventLabel(event.event_type)}</div>
      <div className="truncate text-white/40">{event.target_path ?? 'share root'}</div>
    </div>
    <div className={event.hasProblem() ? 'text-rose-200' : 'text-white/55'}>{event.status}</div>
    <div className="text-white/45">{formatBytes(event.bytes_transferred)}</div>
    <div className="text-white/45">{formatAge(event.created_at)}</div>
  </div>
)

export default function ShareStatsComponent({ vaultId, intervalMs = 7500 }: ShareStatsProps) {
  const [stats, setStats] = useState<VaultShareStats | null>(null)
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
        const next = await useStatsStore.getState().getVaultShareStats({ vault_id: vaultId })
        if (!alive) return
        setStats(next)
        setLastUpdated(Date.now())
      } catch (err: unknown) {
        if (!alive) return
        setError(getErrorMessage(err) || 'Failed to fetch share stats')
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

  const data = useMemo(() => stats ?? new VaultShareStats({ vault_id: vaultId }), [stats, vaultId])
  const problemTone = data.rate_limited_attempts_24h || data.failed_attempts_24h ? tones.danger
    : data.denied_attempts_24h || data.expired_links || data.revoked_links ? tones.warn
    : tones.quiet
  const exposureTone = data.public_links ? tones.warn : data.active_links ? tones.healthy : tones.quiet
  const right = <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />

  return (
    <StatsCard
      title="Share Observatory"
      subtitle="Public and email-validated link usage, upload/download activity, and denied share attempts."
      right={right}
    >
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill
            label="Active links"
            value={formatInt(data.active_links)}
            detail={`${formatInt(data.expired_links)} expired / ${formatInt(data.revoked_links)} revoked`}
            tone={data.active_links ? tones.healthy : tones.quiet}
          />
          <SummaryPill
            label="Exposure"
            value={`${formatInt(data.public_links)} public`}
            detail={`${formatInt(data.email_validated_links)} email validated`}
            tone={exposureTone}
          />
          <SummaryPill
            label="Link churn"
            value={`${formatInt(data.links_created_24h)} / ${formatInt(data.links_revoked_24h)}`}
            detail="created / revoked 24h"
            tone={data.links_created_24h || data.links_revoked_24h ? tones.healthy : tones.quiet}
          />
          <SummaryPill
            label="Downloads"
            value={formatInt(data.downloads_24h)}
            detail="successful 24h"
            tone={data.downloads_24h ? tones.healthy : tones.quiet}
          />
          <SummaryPill
            label="Uploads"
            value={formatInt(data.uploads_24h)}
            detail="completed 24h"
            tone={data.uploads_24h ? tones.healthy : tones.quiet}
          />
          <SummaryPill
            label="Denied/rate"
            value={`${formatInt(data.denied_attempts_24h)} / ${formatInt(data.rate_limited_attempts_24h)}`}
            detail={`${formatInt(data.failed_attempts_24h)} failed attempts`}
            tone={problemTone}
          />
        </div>

        {error ? (
          <div className="rounded-2xl border border-rose-300/25 bg-rose-500/10 px-3 py-2 text-sm text-rose-100">
            {error}
          </div>
        ) : null}

        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-2 flex items-center justify-between gap-3">
              <div className="text-xs font-semibold text-white/75 uppercase">Top links by access</div>
              <div className="text-xs text-white/35">checked {formatAge(data.checked_at)}</div>
            </div>
            {data.top_links_by_access.length ? (
              <div className="space-y-2">
                {data.top_links_by_access.slice(0, 8).map((link) => (
                  <div key={link.share_id} className="grid grid-cols-[1fr_auto] items-center gap-3 rounded-xl border border-white/10 bg-white/[0.03] px-3 py-2 text-xs">
                    <div className="min-w-0">
                      <div className="truncate font-semibold text-white/80">{link.label}</div>
                      <div className="truncate text-white/35">
                        {link.root_path} / {link.link_type} / {link.access_mode}
                      </div>
                    </div>
                    <div className="text-right text-white/55">
                      <div>{formatInt(link.access_count)} hits</div>
                      <div className="text-white/30">
                        {formatInt(link.download_count)} down / {formatInt(link.upload_count)} up
                      </div>
                    </div>
                  </div>
                ))}
              </div>
            ) : (
              <div className="rounded-xl border border-white/10 bg-white/[0.03] px-3 py-3 text-sm text-white/45">
                No share links have been recorded for this vault yet.
              </div>
            )}
          </div>

          <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
            <div className="mb-2 text-xs font-semibold text-white/75 uppercase">Recent share events</div>
            {data.recent_share_events.length ? (
              <div>
                {data.recent_share_events.slice(0, 8).map((event) => (
                  <EventRow key={event.id || `${event.event_type}-${event.created_at}`} event={event} />
                ))}
              </div>
            ) : (
              <div className="rounded-xl border border-white/10 bg-white/[0.03] px-3 py-3 text-sm text-white/45">
                No share access events are available yet.
              </div>
            )}
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
