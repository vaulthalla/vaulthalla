'use client'

import React, { useEffect, useMemo } from 'react'

import { StatsCard } from '@/components/stats/StatsCard'
import type { HealthStatus } from '@/models/stats/systemHealth'
import { useStatsStore } from '@/stores/statsStore'

type Tone = { label: string; border: string; bg: string; text: string; dot: string; soft: string }

const tones: Record<HealthStatus, Tone> = {
  healthy: {
    label: 'healthy',
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    soft: 'text-emerald-200/80',
  },
  degraded: {
    label: 'degraded',
    border: 'border-amber-300/35',
    bg: 'bg-amber-400/10',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    soft: 'text-amber-200/80',
  },
  critical: {
    label: 'critical',
    border: 'border-rose-300/35',
    bg: 'bg-rose-400/10',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
    soft: 'text-rose-200/80',
  },
}

const unknownTone: Tone = {
  label: 'unknown',
  border: 'border-white/10',
  bg: 'bg-white/5',
  text: 'text-white/65',
  dot: 'bg-white/35',
  soft: 'text-white/55',
}

const setupTone: Tone = {
  label: 'setup',
  border: 'border-cyan-300/30',
  bg: 'bg-cyan-400/10',
  text: 'text-cyan-100',
  dot: 'bg-cyan-300',
  soft: 'text-cyan-200/80',
}

const boolTone = (value: boolean): Tone => (value ? tones.healthy : tones.degraded)

function formatCount(ready: number, total: number): string {
  return `${Math.max(0, ready)}/${Math.max(0, total)}`
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
      <span className="text-white/35">·</span>
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
    <div className={['mt-2 text-lg font-semibold', tone.text].join(' ')}>{value}</div>
    {detail ?
      <div className="mt-1 text-xs text-white/45">{detail}</div>
    : null}
  </div>
)

const StatusBadge = ({ label, value }: { label: string; value: boolean | null }) => {
  const tone = value === null ? null : boolTone(value)
  const text =
    value === null ? 'unknown'
    : value ? 'ready'
    : 'missing'

  return (
    <div
      className={[
        'inline-flex items-center gap-2 rounded-full border px-2.5 py-1 text-xs',
        tone ? `${tone.border} ${tone.bg} ${tone.soft}` : 'border-white/10 bg-white/5 text-white/55',
      ].join(' ')}>
      <span className={['h-1.5 w-1.5 rounded-full', tone?.dot ?? 'bg-white/30'].join(' ')} />
      <span className="text-white/60">{label}</span>
      <span className={tone?.text ?? 'text-white/60'}>{text}</span>
    </div>
  )
}

const ShellAdminStatusBadge = ({ value }: { value: boolean | null }) => {
  const tone =
    value === null ? unknownTone
    : value ? tones.healthy
    : setupTone
  const text =
    value === null ? 'unknown'
    : value ? 'bound'
    : 'setup'

  return (
    <div className={['inline-flex items-center gap-2 rounded-full border px-2.5 py-1 text-xs', tone.border, tone.bg, tone.soft].join(' ')}>
      <span className={['h-1.5 w-1.5 rounded-full', tone.dot].join(' ')} />
      <span className="text-white/60">shell admin UID</span>
      <span className={tone.text}>{text}</span>
    </div>
  )
}

const DetailRow = ({ label, value, detail }: { label: string; value: boolean | null; detail?: React.ReactNode }) => {
  const tone = value === null ? null : boolTone(value)
  const text =
    value === null ? 'unknown'
    : value ? 'ready'
    : 'attention'

  return (
    <div className="flex min-h-12 items-center justify-between gap-3 rounded-2xl border border-white/10 bg-white/5 px-3 py-2">
      <div className="min-w-0">
        <div className="truncate text-sm font-medium text-white/85">{label}</div>
        {detail ?
          <div className="mt-0.5 text-xs text-white/45">{detail}</div>
        : null}
      </div>
      <div
        className={[
          'shrink-0 rounded-full border px-2.5 py-1 text-xs',
          tone ? `${tone.border} ${tone.bg} ${tone.text}` : 'border-white/10 bg-white/5 text-white/55',
        ].join(' ')}>
        {text}
      </div>
    </div>
  )
}

const SectionHeader = ({ title, right }: { title: string; right?: React.ReactNode }) => (
  <div className="mb-3 flex items-center justify-between gap-3">
    <div className="text-xs font-semibold text-white/65 uppercase">{title}</div>
    {right ?
      <div className="text-xs text-white/45">{right}</div>
    : null}
  </div>
)

const dependencyRows = [
  ['storage manager', 'storage_manager'],
  ['api key manager', 'api_key_manager'],
  ['auth manager', 'auth_manager'],
  ['session manager', 'session_manager'],
  ['secrets manager', 'secrets_manager'],
  ['sync controller', 'sync_controller'],
  ['fs cache', 'fs_cache'],
  ['shell usage manager', 'shell_usage_manager'],
  ['http cache stats', 'http_cache_stats'],
  ['fuse session', 'fuse_session'],
] as const

export default function SystemHealthComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.systemHealth)
  const startPolling = useStatsStore(s => s.startSystemHealthPolling)
  const stopPolling = useStatsStore(s => s.stopSystemHealthPolling)

  useEffect(() => {
    startPolling(intervalMs)
    return () => stopPolling()
  }, [startPolling, stopPolling, intervalMs])

  const health = wrapper.data
  const statusTone = tones[health.overall_status] ?? tones.degraded
  const checkedAt = formatCheckedAt(health.summary.checked_at, wrapper.lastUpdated)

  const derived = useMemo(() => {
    const servicesTotal =
      health.summary.services_total || health.runtime.service_count || health.runtime.services.length
    const servicesReady =
      health.summary.services_ready || health.runtime.services.filter(service => service.running).length
    const depsTotal = health.summary.deps_total || dependencyRows.length - 1
    const depsReady = health.summary.deps_ready
    const protocolsTotal = health.summary.protocols_total || health.protocols.configuredTotal()
    const protocolsReady = health.summary.protocols_ready || health.protocols.readyTotal()

    return { servicesReady, servicesTotal, depsReady, depsTotal, protocolsReady, protocolsTotal }
  }, [health])

  const right = <LiveBadge loading={!!wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />

  return (
    <StatsCard
      title="System Health"
      subtitle="Runtime readiness and dependency sanity"
      right={right}
      className="w-full">
      <div className="space-y-4">
        <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-6">
          <SummaryPill label="Overall" value={statusTone.label} detail={`checked ${checkedAt}`} tone={statusTone} />
          <SummaryPill
            label="Services"
            value={formatCount(derived.servicesReady, derived.servicesTotal)}
            detail="running"
            tone={
              derived.servicesReady === derived.servicesTotal && derived.servicesTotal > 0 ?
                tones.healthy
              : tones.degraded
            }
          />
          <SummaryPill
            label="Protocols"
            value={formatCount(derived.protocolsReady, derived.protocolsTotal)}
            detail={derived.protocolsTotal > 0 ? 'ready' : 'configured'}
            tone={derived.protocolsReady === derived.protocolsTotal ? tones.healthy : tones.degraded}
          />
          <SummaryPill
            label="Deps"
            value={formatCount(derived.depsReady, derived.depsTotal)}
            detail="initialized"
            tone={derived.depsReady === derived.depsTotal ? tones.healthy : tones.degraded}
          />
          <SummaryPill
            label="FUSE"
            value={health.deps.fuse_session ? 'present' : 'missing'}
            detail="session"
            tone={boolTone(health.deps.fuse_session)}
          />
          <SummaryPill
            label="Shell UID"
            value={
              health.shell.admin_uid_bound === null ? 'unknown'
              : health.shell.admin_uid_bound ?
                'bound'
              : 'setup'
            }
            detail={health.shell.admin_uid_bound === false ? 'setup advisory' : 'admin'}
            tone={
              health.shell.admin_uid_bound === null ? unknownTone
              : health.shell.admin_uid_bound ?
                tones.healthy
              : setupTone
            }
          />
        </div>

        {health.shell.admin_uid_bound === false ?
          <div className="rounded-2xl border border-cyan-300/25 bg-cyan-400/10 px-3 py-2 text-xs text-cyan-100">
            CLI shell admin UID is not configured. Runtime health is not degraded by this setup advisory.
          </div>
        : null}

        {wrapper.error ?
          <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs text-rose-200/90">
            {wrapper.error}
          </div>
        : null}

        <div className="grid grid-cols-1 gap-4 xl:grid-cols-[1.15fr_0.85fr]">
          <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
            <SectionHeader
              title="Runtime Services"
              right={`${formatCount(derived.servicesReady, derived.servicesTotal)} running`}
            />
            <div className="grid grid-cols-1 gap-2 md:grid-cols-2">
              {health.runtime.services.map(service => (
                <DetailRow
                  key={`${service.entry_name}:${service.service_name}`}
                  label={service.entry_name}
                  value={service.running && !service.interrupted}
                  detail={
                    <span>
                      {service.service_name}
                      {service.interrupted ?
                        <span className="text-amber-200/80"> · interrupt requested</span>
                      : null}
                    </span>
                  }
                />
              ))}
              {health.runtime.services.length === 0 ?
                <DetailRow label="runtime services" value={false} detail="no service status reported" />
              : null}
            </div>
          </div>

          <div className="space-y-4">
            <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
              <SectionHeader
                title="Protocols"
                right={`${formatCount(derived.protocolsReady, derived.protocolsTotal)} ready`}
              />
              <div className="space-y-2">
                <div className="flex flex-wrap gap-2">
                  <StatusBadge label="service" value={health.protocols.running} />
                  <StatusBadge label="io context" value={health.protocols.io_context_initialized} />
                </div>
                <DetailRow
                  label="Websocket"
                  value={health.protocols.websocket_configured ? health.protocols.websocket_ready : null}
                  detail={health.protocols.websocket_configured ? 'configured' : 'not configured'}
                />
                <DetailRow
                  label="HTTP preview"
                  value={health.protocols.http_preview_configured ? health.protocols.http_preview_ready : null}
                  detail={health.protocols.http_preview_configured ? 'configured' : 'not configured'}
                />
              </div>
            </div>

            <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
              <SectionHeader title="Session" />
              <div className="flex flex-wrap gap-2">
                <StatusBadge label="FUSE" value={health.deps.fuse_session} />
                <ShellAdminStatusBadge value={health.shell.admin_uid_bound} />
              </div>
            </div>
          </div>
        </div>

        <div className="rounded-2xl border border-white/10 bg-white/5 p-3 backdrop-blur">
          <SectionHeader
            title="Dependencies"
            right={`${formatCount(derived.depsReady, derived.depsTotal)} core ready`}
          />
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-5">
            {dependencyRows.map(([label, key]) => (
              <DetailRow key={key} label={label} value={health.deps[key]} />
            ))}
          </div>
        </div>
      </div>
    </StatsCard>
  )
}
