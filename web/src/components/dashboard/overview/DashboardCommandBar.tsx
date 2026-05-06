'use client'

import Link from 'next/link'

import type {
  DashboardCardSize,
  DashboardCardVariant,
} from '@/models/dashboard/dashboardLayout'
import type { DashboardMetricSummary, DashboardSeverity } from '@/models/stats/dashboardOverview'
import type { DashboardCardCatalogItem, DashboardLayoutPreset } from '@/components/dashboard/dashboardCardCatalog'
import { dashboardCardCatalog, dashboardLayoutPresets } from '@/components/dashboard/dashboardCardCatalog'
import { DashboardSeverityBadge, DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardSeverityTone } from '@/components/dashboard/dashboardSeverity'
import GridPlusIcon from '@/fa-duotone/grid-2-plus.svg'
import { DashboardCardPicker } from '@/components/dashboard/overview/DashboardCardPicker'

function LiveBadge({
  loading,
  error,
  lastUpdated,
}: {
  loading: boolean
  error: string | null
  lastUpdated: number | null
}) {
  return (
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
}

function CommandHealthMetric({ metric }: { metric: DashboardMetricSummary }) {
  const tone = dashboardSeverityTone(metric.tone)

  return (
    <Link
      href={metric.href || '/dashboard/runtime#system-health'}
      className={[
        'inline-flex items-center gap-2 rounded-full border bg-black/20 px-2.5 py-1 text-xs transition hover:brightness-125',
        tone.border,
        tone.bg,
      ].join(' ')}>
      <span className="text-white/45">{metric.label}</span>
      <span className={['font-semibold', tone.text].join(' ')}>{metric.value || 'unknown'}</span>
    </Link>
  )
}

export function DashboardCommandBar({
  overallStatus,
  errorCount,
  warningCount,
  loading,
  error,
  lastUpdated,
  checkedAt,
  commandHealthMetrics,
  shellSetupAdvisory,
  customizing,
  selectedPresetId,
  cardPickerOpen,
  addableCatalog,
  visibleCombos,
  pickerSize,
  pickerVariant,
  preferenceSaving,
  layoutDirty,
  needsMigrationSave,
  onCustomize,
  onPresetChange,
  onToggleCardPicker,
  onPickerSizeChange,
  onPickerVariantChange,
  onAddCard,
  onReset,
  onSave,
  onCancel,
  onDone,
}: {
  overallStatus: DashboardSeverity
  errorCount: number
  warningCount: number
  loading: boolean
  error: string | null
  lastUpdated: number | null
  checkedAt: string
  commandHealthMetrics: DashboardMetricSummary[]
  shellSetupAdvisory: DashboardMetricSummary | null
  customizing: boolean
  selectedPresetId: string
  cardPickerOpen: boolean
  addableCatalog: DashboardCardCatalogItem[]
  visibleCombos: Set<string>
  pickerSize: DashboardCardSize
  pickerVariant: DashboardCardVariant
  preferenceSaving: boolean
  layoutDirty: boolean
  needsMigrationSave: boolean
  onCustomize: () => void
  onPresetChange: (preset: DashboardLayoutPreset) => void
  onToggleCardPicker: () => void
  onPickerSizeChange: (size: DashboardCardSize) => void
  onPickerVariantChange: (variant: DashboardCardVariant) => void
  onAddCard: (id: string, size: DashboardCardSize, variant: DashboardCardVariant) => void
  onReset: () => void
  onSave: () => void
  onCancel: () => void
  onDone: () => void
}) {
  return (
    <div className="flex flex-col gap-4 xl:flex-row xl:items-start xl:justify-between">
      <div className="min-w-0">
        <div className="flex flex-wrap items-center gap-2">
          <h1 className="text-2xl font-semibold text-white">Health Command Center</h1>
          <DashboardSeverityBadge
            severity={overallStatus}
            errorCount={errorCount}
            warningCount={warningCount}
            showCount
          />
          <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
            {errorCount} errors
          </span>
          <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
            {warningCount} warnings
          </span>
          <LiveBadge loading={loading} error={error} lastUpdated={lastUpdated} />
        </div>
        <p className="mt-2 max-w-3xl text-sm text-white/60">
          System Health is pinned here. Configure the operational cards below and use drilldowns for full detail.
        </p>
        {commandHealthMetrics.length ?
          <div className="mt-2 flex flex-wrap gap-2">
            {commandHealthMetrics.map(metric => (
              <CommandHealthMetric key={metric.key} metric={metric} />
            ))}
          </div>
        : null}
        {shellSetupAdvisory ?
          <div className="mt-2 inline-flex items-center gap-2 rounded-full border border-cyan-300/25 bg-cyan-400/10 px-3 py-1.5 text-xs text-cyan-100">
            <DashboardSeverityIcon severity="info" className="h-3.5 w-3.5 text-cyan-100" />
            CLI shell admin UID is not configured.
          </div>
        : null}
      </div>

      <div className="flex shrink-0 flex-wrap items-center gap-2 xl:justify-end">
        <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
          checked {checkedAt}
        </span>
        {customizing ?
          <>
            <select
              className="rounded-full border border-white/10 bg-zinc-950/80 px-2.5 py-1 text-xs text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50"
              value={selectedPresetId}
              onChange={event => {
                const preset = dashboardLayoutPresets.find(item => item.id === event.target.value)
                if (preset) onPresetChange(preset)
              }}>
              {dashboardLayoutPresets.map(preset => (
                <option key={preset.id} value={preset.id}>
                  {preset.title}
                </option>
              ))}
            </select>
            <div className="relative">
              <button
                type="button"
                className="inline-flex items-center gap-1.5 rounded-full border border-cyan-200/25 bg-cyan-400/10 px-2.5 py-1 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45 disabled:cursor-not-allowed disabled:opacity-40"
                onClick={onToggleCardPicker}
                disabled={!addableCatalog.length}>
                <GridPlusIcon className="h-3.5 w-3.5 fill-current" aria-hidden="true" />
                Add Card
              </button>
              {cardPickerOpen ?
                <DashboardCardPicker
                  catalog={dashboardCardCatalog}
                  visibleCombos={visibleCombos}
                  selectedSize={pickerSize}
                  selectedVariant={pickerVariant}
                  onSizeChange={onPickerSizeChange}
                  onVariantChange={onPickerVariantChange}
                  onAdd={onAddCard}
                />
              : null}
            </div>
            <button
              type="button"
              className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/70 transition hover:border-amber-200/35 hover:text-amber-100"
              onClick={onReset}
              disabled={preferenceSaving}>
              Reset
            </button>
            <button
              type="button"
              className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-2.5 py-1 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45 disabled:cursor-not-allowed disabled:opacity-40"
              onClick={onSave}
              disabled={preferenceSaving || (!layoutDirty && !needsMigrationSave)}>
              {preferenceSaving ? 'Saving...' : 'Save'}
            </button>
            <button
              type="button"
              className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/70 transition hover:border-white/25 hover:text-white"
              onClick={onCancel}
              disabled={preferenceSaving}>
              Cancel
            </button>
            <button
              type="button"
              className="rounded-full border border-emerald-200/25 bg-emerald-400/10 px-2.5 py-1 text-xs font-medium text-emerald-100 transition hover:border-emerald-100/45"
              onClick={onDone}
              disabled={preferenceSaving}>
              {layoutDirty || needsMigrationSave ? 'Save & Done' : 'Done'}
            </button>
          </>
        : <button
            type="button"
            className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-3 py-1.5 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45"
            onClick={onCustomize}>
            Customize
          </button>
        }
      </div>
    </div>
  )
}
