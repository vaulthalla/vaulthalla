import React, { useEffect, useMemo, useState } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import { VaultActivity, VaultActivityEvent } from '@/models/stats/vaultActivity'
import { useStatsStore } from '@/stores/statsStore'
import { getErrorMessage } from '@/util/handleErrors'

type VaultActivityProps = { vaultId: number; intervalMs?: number }
type Tone = { border: string; bg: string; text: string; dot: string }

const tones = {
  active: {
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

function formatTime(value: number | string | null): string {
  const ms = toMs(value)
  return ms == null ? 'unknown' : new Date(ms).toLocaleString()
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

const EventRow = ({ event }: { event: VaultActivityEvent }) => (
  <div className="grid grid-cols-1 gap-2 border-t border-white/10 px-1 py-2 text-xs text-white/70 sm:grid-cols-[90px_1fr_120px_100px]">
    <div className="font-semibold text-white/85">{event.action}</div>
    <div className="min-w-0 truncate text-white/55">{event.path}</div>
    <div className="text-white/45">{event.user_name ?? (event.user_id == null ? 'unknown' : `uid ${event.user_id}`)}</div>
    <div className={event.status === 'error' ? 'text-rose-200' : 'text-white/45'}>{event.status ?? event.source}</div>
  </div>
)

export default function VaultActivityComponent({ vaultId, intervalMs = 7500 }: VaultActivityProps) {
  const [activity, setActivity] = useState<VaultActivity | null>(null)
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
        const next = await useStatsStore.getState().getVaultActivity({ vault_id: vaultId })
        if (!alive) return
        setActivity(next)
        setLastUpdated(Date.now())
      } catch (err: unknown) {
        if (!alive) return
        setError(getErrorMessage(err) || 'Failed to fetch vault activity')
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

  const data = useMemo(() => activity ?? new VaultActivity({ vault_id: vaultId }), [activity, vaultId])
  const hasRecentFailures = data.recent_activity.some((event) => event.status === 'error' || !!event.error)
  const overallTone = hasRecentFailures ? tones.danger : data.hasActivity() ? tones.active : tones.quiet
  const right = <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />

  return (
    <StatsCard
      title="Activity"
      subtitle="Recent vault mutations from file activity, trash, and operation records."
      right={right}
    >
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill
            label="Mutations"
            value={formatInt(data.totalMutations24h())}
            detail="last 24h"
            tone={overallTone}
          />
          <SummaryPill
            label="Last activity"
            value={formatAge(data.last_activity_at)}
            detail={data.last_activity_action ?? 'no activity recorded'}
            tone={data.last_activity_at ? tones.active : tones.quiet}
          />
          <SummaryPill
            label="Uploads"
            value={`${formatInt(data.uploads_24h)} / ${formatInt(data.uploads_7d)}`}
            detail="24h / 7d"
            tone={data.uploads_24h ? tones.active : tones.quiet}
          />
          <SummaryPill
            label="Deletes"
            value={`${formatInt(data.deletes_24h)} / ${formatInt(data.deletes_7d)}`}
            detail="24h / 7d"
            tone={data.deletes_24h ? tones.warn : tones.quiet}
          />
          <SummaryPill
            label="Move/rename"
            value={formatInt(data.moves_24h + data.renames_24h)}
            detail={`${formatInt(data.copies_24h)} copies`}
            tone={data.moves_24h || data.renames_24h || data.copies_24h ? tones.active : tones.quiet}
          />
          <SummaryPill
            label="Bytes changed"
            value={formatBytes(data.bytes_added_24h + data.bytes_removed_24h)}
            detail={`${formatBytes(data.bytes_added_24h)} in / ${formatBytes(data.bytes_removed_24h)} out`}
            tone={data.bytes_added_24h || data.bytes_removed_24h ? tones.active : tones.quiet}
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
              <div className="text-xs font-semibold text-white/75 uppercase">Recent activity</div>
              <div className="text-xs text-white/35">{formatTime(data.checked_at)}</div>
            </div>
            {data.recent_activity.length ? (
              <div>
                {data.recent_activity.slice(0, 8).map((event, index) => (
                  <EventRow key={`${event.source}-${event.action}-${event.path}-${event.occurred_at}-${index}`} event={event} />
                ))}
              </div>
            ) : (
              <div className="rounded-xl border border-white/10 bg-white/[0.03] px-3 py-3 text-sm text-white/45">
                No file activity, trash, or operation records are available yet.
              </div>
            )}
          </div>

          <div className="grid grid-cols-1 gap-3">
            <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
              <div className="mb-2 text-xs font-semibold text-white/75 uppercase">Top touched paths</div>
              {data.top_touched_paths.length ? (
                <div className="space-y-2">
                  {data.top_touched_paths.slice(0, 5).map((path) => (
                    <div key={`${path.action}-${path.path}`} className="flex items-center justify-between gap-3 text-xs">
                      <div className="min-w-0">
                        <div className="truncate font-semibold text-white/75">{path.path}</div>
                        <div className="text-white/35">{path.action}</div>
                      </div>
                      <div className="shrink-0 text-right text-white/55">
                        <div>{formatInt(path.count)}</div>
                        <div className="text-white/30">{formatBytes(path.bytes)}</div>
                      </div>
                    </div>
                  ))}
                </div>
              ) : (
                <div className="text-sm text-white/45">No touched-path data yet.</div>
              )}
            </div>

            <div className="rounded-2xl border border-white/10 bg-black/10 p-3">
              <div className="mb-2 text-xs font-semibold text-white/75 uppercase">Top active users</div>
              {data.top_active_users.length ? (
                <div className="space-y-2">
                  {data.top_active_users.slice(0, 5).map((user) => (
                    <div key={`${user.user_id ?? 'unknown'}-${user.user_name ?? 'unknown'}`} className="flex items-center justify-between gap-3 text-xs">
                      <span className="truncate font-semibold text-white/75">{user.label()}</span>
                      <span className="shrink-0 text-white/50">{formatInt(user.count)}</span>
                    </div>
                  ))}
                </div>
              ) : (
                <div className="text-sm text-white/45">No user attribution has been recorded yet.</div>
              )}
            </div>
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
