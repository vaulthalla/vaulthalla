'use client'

import Link from 'next/link'
import React, { useCallback, useEffect, useMemo, useState } from 'react'

import {
  DASHBOARD_LAYOUT_STORAGE_KEY,
  dashboardLayoutPreference,
  type DashboardCardSize,
  type DashboardCardVariant,
  type DashboardLayoutCard,
  normalizeDashboardLayout,
  visibleDashboardLayoutCards,
} from '@/models/dashboard/dashboardLayout'
import { DASHBOARD_HOME_PREFERENCE_KEY } from '@/models/dashboard/dashboardPreferences'
import {
  DashboardCardSummary,
  type DashboardMetricSummary,
  type DashboardOverviewRequest,
  type DashboardSectionSummary,
} from '@/models/stats/dashboardOverview'
import {
  dashboardCardCatalog,
  dashboardCardCatalogById,
  dashboardLayoutFromCards,
  dashboardLayoutPresets,
  defaultDashboardLayout,
  type DashboardCardCatalogItem,
  type DashboardLayoutPreset,
} from '@/components/dashboard/dashboardCardCatalog'
import { DashboardIssueList, DashboardIssueSummaryLine } from '@/components/dashboard/DashboardIssueList'
import { DashboardSeverityBadge, DashboardSeverityIcon } from '@/components/dashboard/DashboardSeverityBadge'
import { dashboardSeverityTone, sortDashboardIssues } from '@/components/dashboard/dashboardSeverity'
import { useDashboardPreferencesStore } from '@/stores/dashboardPreferencesStore'
import { useStatsStore } from '@/stores/statsStore'

function formatCheckedAt(value: number | string | null, fallback: number | null): string {
  if (typeof value === 'number' && Number.isFinite(value) && value > 0) {
    const ms = value > 1_000_000_000_000 ? value : value * 1000
    return new Date(ms).toLocaleTimeString()
  }

  if (typeof value === 'string' && value.length > 0) {
    const parsed = Date.parse(value)
    if (Number.isFinite(parsed)) return new Date(parsed).toLocaleTimeString()
  }

  return fallback ? new Date(fallback).toLocaleTimeString() : 'pending'
}

function makeDefaultLayout(): DashboardLayoutCard[] {
  const defaults = defaultDashboardLayout()
  return normalizeDashboardLayout(defaults, dashboardCardCatalog, defaults)
}

function loadStoredLayout(): DashboardLayoutCard[] {
  const defaults = makeDefaultLayout()
  if (typeof window === 'undefined') return defaults

  try {
    const raw = window.localStorage.getItem(DASHBOARD_LAYOUT_STORAGE_KEY)
    if (!raw) return defaults
    return normalizeDashboardLayout(JSON.parse(raw), dashboardCardCatalog, defaults)
  } catch {
    return defaults
  }
}

function storeLayout(layout: DashboardLayoutCard[]) {
  if (typeof window === 'undefined') return
  window.localStorage.setItem(DASHBOARD_LAYOUT_STORAGE_KEY, JSON.stringify(dashboardLayoutPreference(layout)))
}

function layoutKey(layout: DashboardLayoutCard[]): string {
  return JSON.stringify(dashboardLayoutPreference(layout).cards)
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

function MetricPill({ metric, link = true }: { metric: DashboardMetricSummary; link?: boolean }) {
  const tone = dashboardSeverityTone(metric.tone)
  const body = (
    <div className={['rounded-2xl border px-3 py-2', tone.border, tone.bg].join(' ')}>
      <div className="text-[11px] text-white/45 uppercase">{metric.label}</div>
      <div className={['mt-1 truncate text-sm font-semibold', tone.text].join(' ')}>{metric.value || 'unknown'}</div>
    </div>
  )

  return link && metric.href ?
      <Link href={metric.href} className="transition hover:brightness-125">
        {body}
      </Link>
    : body
}

function OverviewShell({
  children,
  className,
}: {
  children: React.ReactNode
  className?: string
}) {
  return (
    <section
      className={[
        'relative overflow-hidden rounded-3xl border border-white/10 bg-zinc-950/55 p-5 shadow-[0_20px_60px_-25px_rgba(0,0,0,0.9)] backdrop-blur-xl',
        'before:pointer-events-none before:absolute before:inset-0 before:bg-[radial-gradient(900px_circle_at_15%_10%,rgba(255,255,255,0.08),transparent_45%),radial-gradient(700px_circle_at_85%_25%,rgba(56,189,248,0.09),transparent_55%)]',
        className ?? '',
      ].join(' ')}>
      <div className="relative z-10">{children}</div>
    </section>
  )
}

function SectionCard({ section }: { section: DashboardSectionSummary }) {
  const tone = dashboardSeverityTone(section.severity)
  const firstIssue = sortDashboardIssues([...section.errors, ...section.warnings])[0]

  return (
    <Link href={section.href} className="block h-full transition hover:-translate-y-0.5 hover:brightness-110">
      <div
        className={[
          'h-full rounded-3xl border bg-zinc-950/45 p-4 backdrop-blur',
          tone.border,
          tone.ring,
          section.error_count > 0 || section.warning_count > 0 ? 'ring-1 ring-inset' : '',
          section.error_count > 0 ? 'ring-rose-300/15'
          : section.warning_count > 0 ? 'ring-amber-300/15'
          : 'ring-white/0',
        ].join(' ')}>
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0">
            <div className="flex items-center gap-2 text-sm font-semibold text-white/90">
              <DashboardSeverityIcon severity={section.severity} className={['h-4 w-4', tone.text].join(' ')} />
              {section.title}
            </div>
            <div className="mt-1 text-xs text-white/50">{section.description}</div>
            <DashboardIssueSummaryLine
              severity={section.error_count > 0 ? 'error' : section.warning_count > 0 ? 'warning' : section.severity}
              errorCount={section.error_count}
              warningCount={section.warning_count}
              firstIssue={firstIssue}
            />
          </div>
          <DashboardSeverityBadge
            severity={section.severity}
            errorCount={section.error_count}
            warningCount={section.warning_count}
            showCount
          />
        </div>
        <p className="mt-4 min-h-10 text-sm text-white/70">{section.summary}</p>
        <div className="mt-4 grid grid-cols-2 gap-2">
          {section.metrics.slice(0, 4).map(metric => (
            <MetricPill key={`${section.id}-${metric.key}-${metric.label}`} metric={metric} link={false} />
          ))}
        </div>
      </div>
    </Link>
  )
}

function gridClassForSize(size: DashboardCardSize): string {
  if (size === '1x1') return 'xl:col-span-3'
  if (size === '2x1') return 'xl:col-span-6'
  if (size === '2x2') return 'xl:col-span-6'
  if (size === '3x2') return 'xl:col-span-9'
  return 'xl:col-span-12'
}

function minHeightForSize(size: DashboardCardSize): string {
  if (size === '1x1') return 'min-h-[13rem]'
  if (size === '2x1') return 'min-h-[14rem]'
  if (size === '2x2') return 'min-h-[19rem]'
  if (size === '3x2') return 'min-h-[20rem]'
  return 'min-h-[21rem]'
}

function metricCountForCard(layoutCard: DashboardLayoutCard): number {
  if (layoutCard.variant === 'hero' || layoutCard.size === '4x2') return 6
  if (layoutCard.size === '2x2' || layoutCard.size === '3x2') return 4
  return 2
}

function issueCountForCard(layoutCard: DashboardLayoutCard): number {
  if (layoutCard.variant === 'hero' || layoutCard.size === '4x2') return 3
  if (layoutCard.variant === 'summary' || layoutCard.size === '2x2' || layoutCard.size === '3x2') return 2
  return 1
}

function pendingCardForLayout(layoutCard: DashboardLayoutCard, catalogItem: DashboardCardCatalogItem): DashboardCardSummary {
  return new DashboardCardSummary({
    id: catalogItem.id,
    section_id: catalogItem.sectionId,
    title: catalogItem.title,
    description: catalogItem.description,
    href: catalogItem.href,
    variant: layoutCard.variant,
    size: layoutCard.size,
    severity: 'unknown',
    available: true,
    summary: 'Waiting for backend summary.',
    metrics: [],
    warnings: [],
    errors: [],
    checked_at: null,
  })
}

function CardCustomizationControls({
  layoutCard,
  catalogItem,
  index,
  total,
  onMove,
  onDragStart,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  index: number
  total: number
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  const buttonClass =
    'rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/65 transition hover:border-cyan-200/30 hover:text-cyan-100 disabled:cursor-not-allowed disabled:opacity-35'
  const selectClass =
    'rounded-full border border-white/10 bg-zinc-950/80 px-2.5 py-1 text-xs text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50'

  return (
    <div className="mb-4 flex flex-wrap items-center gap-2 rounded-2xl border border-white/10 bg-black/20 p-2">
      <button
        type="button"
        draggable
        className="cursor-grab rounded-full border border-cyan-200/20 bg-cyan-400/10 px-2.5 py-1 text-xs text-cyan-100 transition hover:border-cyan-100/45 active:cursor-grabbing"
        title="Drag to reorder"
        onDragStart={event => onDragStart(layoutCard.id, event)}>
        Drag
      </button>
      <button type="button" className={buttonClass} onClick={() => onMove(layoutCard.id, -1)} disabled={index === 0}>
        Up
      </button>
      <button
        type="button"
        className={buttonClass}
        onClick={() => onMove(layoutCard.id, 1)}
        disabled={index >= total - 1}>
        Down
      </button>
      <label className="flex items-center gap-2 text-xs text-white/45">
        Size
        <select
          className={selectClass}
          value={layoutCard.size}
          onChange={event => onSizeChange(layoutCard.id, event.target.value as DashboardCardSize)}>
          {catalogItem.supportedSizes.map(size => (
            <option key={size} value={size}>
              {size}
            </option>
          ))}
        </select>
      </label>
      <label className="flex items-center gap-2 text-xs text-white/45">
        Variant
        <select
          className={selectClass}
          value={layoutCard.variant}
          onChange={event => onVariantChange(layoutCard.id, event.target.value as DashboardCardVariant)}>
          {catalogItem.supportedVariants.map(variant => (
            <option key={variant} value={variant}>
              {variant}
            </option>
          ))}
        </select>
      </label>
      <button type="button" className={buttonClass} onClick={() => onRemove(layoutCard.id)} disabled={total <= 1}>
        Remove
      </button>
    </div>
  )
}

function SummaryCard({
  card,
  layoutCard,
  catalogItem,
  customizing,
  index,
  total,
  onMove,
  onDragStart,
  onDragOverCard,
  onDropCard,
  onDragEnd,
  dragging,
  dragOver,
  onRemove,
  onSizeChange,
  onVariantChange,
}: {
  card: DashboardCardSummary
  layoutCard: DashboardLayoutCard
  catalogItem: DashboardCardCatalogItem
  customizing: boolean
  index: number
  total: number
  onMove: (id: string, direction: -1 | 1) => void
  onDragStart: (id: string, event: React.DragEvent<HTMLElement>) => void
  onDragOverCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDropCard: (id: string, event: React.DragEvent<HTMLDivElement>) => void
  onDragEnd: () => void
  dragging: boolean
  dragOver: boolean
  onRemove: (id: string) => void
  onSizeChange: (id: string, size: DashboardCardSize) => void
  onVariantChange: (id: string, variant: DashboardCardVariant) => void
}) {
  const tone = dashboardSeverityTone(card.severity)
  const errors = card.errors.length
  const warnings = card.warnings.length
  const firstIssue = sortDashboardIssues([...card.errors, ...card.warnings])[0]
  const isCompact = layoutCard.variant === 'compact' || layoutCard.size === '1x1' || layoutCard.size === '2x1'
  const metricCount = metricCountForCard(layoutCard)
  const issueCount = issueCountForCard(layoutCard)

  return (
    <div
      className={['col-span-1', gridClassForSize(layoutCard.size)].join(' ')}
      onDragOver={event => onDragOverCard(layoutCard.id, event)}
      onDrop={event => onDropCard(layoutCard.id, event)}
      onDragEnd={onDragEnd}>
      <article
        className={[
          'h-full rounded-3xl border bg-zinc-950/45 p-4 backdrop-blur transition hover:-translate-y-0.5 hover:brightness-110',
          minHeightForSize(layoutCard.size),
          tone.border,
          tone.ring,
          dragging ? 'opacity-55' : '',
          dragOver ? 'outline outline-2 outline-cyan-200/60' : '',
          errors > 0 || warnings > 0 ? 'ring-1 ring-inset' : '',
          errors > 0 ? 'ring-rose-300/15'
          : warnings > 0 ? 'ring-amber-300/15'
          : 'ring-white/0',
        ].join(' ')}>
        {customizing ?
          <CardCustomizationControls
            layoutCard={layoutCard}
            catalogItem={catalogItem}
            index={index}
            total={total}
            onMove={onMove}
            onDragStart={onDragStart}
            onRemove={onRemove}
            onSizeChange={onSizeChange}
            onVariantChange={onVariantChange}
          />
        : null}

        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0">
            <div className={['flex items-center gap-2 font-semibold text-white/90', isCompact ? 'text-sm' : 'text-base'].join(' ')}>
              <DashboardSeverityIcon severity={card.severity} className={['h-4 w-4', tone.text].join(' ')} />
              {card.title}
            </div>
            {!isCompact ?
              <div className="mt-1 line-clamp-2 text-xs text-white/50">{card.description || catalogItem.description}</div>
            : null}
            <DashboardIssueSummaryLine
              severity={errors > 0 ? 'error' : warnings > 0 ? 'warning' : card.severity}
              errorCount={errors}
              warningCount={warnings}
              firstIssue={firstIssue}
            />
          </div>
          <DashboardSeverityBadge severity={card.severity} errorCount={errors} warningCount={warnings} showCount />
        </div>

        <p className={['mt-4 text-sm text-white/70', isCompact ? 'line-clamp-2 min-h-10' : 'min-h-12'].join(' ')}>
          {card.available ? card.summary : card.unavailable_reason}
        </p>

        <div className={['mt-4 grid gap-2', isCompact ? 'grid-cols-2' : 'grid-cols-2 lg:grid-cols-3'].join(' ')}>
          {card.metrics.slice(0, metricCount).map(metric => (
            <MetricPill key={`${card.id}-${metric.key}`} metric={metric} link={false} />
          ))}
        </div>

        {card.errors.length || card.warnings.length ?
          <div className="mt-4">
            <DashboardIssueList issues={[...card.errors, ...card.warnings]} max={issueCount} link={false} compact={isCompact} />
          </div>
        : null}

        <div className="mt-4">
          <Link href={card.href || catalogItem.href} className="text-xs font-medium text-cyan-100/80 transition hover:text-cyan-50">
            View details {'>'}
          </Link>
        </div>
      </article>
    </div>
  )
}

function buildOverviewPayload(layout: DashboardLayoutCard[]): DashboardOverviewRequest {
  return {
    scope: 'system',
    mode: 'dashboard_home',
    cards: visibleDashboardLayoutCards(layout).map(card => ({
      id: card.id,
      size: card.size,
      variant: card.variant,
    })),
  }
}

export default function DashboardOverviewComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const wrapper = useStatsStore(s => s.dashboardOverview)
  const startPolling = useStatsStore(s => s.startDashboardOverviewPolling)
  const refreshOverview = useStatsStore(s => s.refreshDashboardOverview)
  const preferenceLoading = useDashboardPreferencesStore(s => s.loading)
  const preferenceSaving = useDashboardPreferencesStore(s => s.saving)
  const preferenceError = useDashboardPreferencesStore(s => s.error)
  const loadPreference = useDashboardPreferencesStore(s => s.getPreference)
  const savePreference = useDashboardPreferencesStore(s => s.savePreference)
  const resetPreference = useDashboardPreferencesStore(s => s.resetPreference)
  const [customizing, setCustomizing] = useState(false)
  const [layoutLoaded, setLayoutLoaded] = useState(false)
  const [layout, setLayout] = useState<DashboardLayoutCard[]>(() => makeDefaultLayout())
  const [savedLayout, setSavedLayout] = useState<DashboardLayoutCard[]>(() => makeDefaultLayout())
  const [savedLayoutKey, setSavedLayoutKey] = useState(() => layoutKey(makeDefaultLayout()))
  const [needsMigrationSave, setNeedsMigrationSave] = useState(false)
  const [saveNotice, setSaveNotice] = useState<string | null>(null)
  const [selectedAddId, setSelectedAddId] = useState('')
  const [selectedPresetId, setSelectedPresetId] = useState('default')
  const [draggedCardId, setDraggedCardId] = useState<string | null>(null)
  const [dragOverCardId, setDragOverCardId] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    const localLayout = loadStoredLayout()
    const localKey = layoutKey(localLayout)

    setLayout(localLayout)
    setSavedLayout(localLayout)
    setSavedLayoutKey(localKey)
    setLayoutLoaded(true)

    void loadPreference({ preference_key: DASHBOARD_HOME_PREFERENCE_KEY })
      .then(preference => {
        if (cancelled) return

        if (preference.exists && preference.layout) {
          const serverLayout = normalizeDashboardLayout(preference.layout, dashboardCardCatalog, makeDefaultLayout())
          const serverKey = layoutKey(serverLayout)
          setLayout(serverLayout)
          setSavedLayout(serverLayout)
          setSavedLayoutKey(serverKey)
          setNeedsMigrationSave(false)
          storeLayout(serverLayout)
          void refreshOverview(buildOverviewPayload(serverLayout))
          return
        }

        const hasLocalLayout = typeof window !== 'undefined' && Boolean(window.localStorage.getItem(DASHBOARD_LAYOUT_STORAGE_KEY))
        setNeedsMigrationSave(hasLocalLayout)
      })
      .catch(() => {
        if (!cancelled) setNeedsMigrationSave(false)
      })

    return () => {
      cancelled = true
    }
  }, [loadPreference, refreshOverview])

  const catalogById = useMemo(() => dashboardCardCatalogById(), [])
  const visibleLayout = useMemo(() => visibleDashboardLayoutCards(layout), [layout])
  const visibleIds = useMemo(() => new Set(visibleLayout.map(card => card.id)), [visibleLayout])
  const visibleSectionIds = useMemo(() => {
    const sectionIds = new Set<string>()
    for (const card of visibleLayout) {
      const sectionId = catalogById.get(card.id)?.sectionId
      if (sectionId) sectionIds.add(sectionId)
    }
    return sectionIds
  }, [catalogById, visibleLayout])
  const hiddenCatalog = useMemo(
    () => dashboardCardCatalog.filter(card => !visibleIds.has(card.id) && card.available),
    [visibleIds],
  )
  const overviewPayload = useMemo(() => buildOverviewPayload(layout), [layout])
  const payloadKey = useMemo(() => JSON.stringify(overviewPayload.cards), [overviewPayload.cards])
  const currentLayoutKey = useMemo(() => layoutKey(layout), [layout])
  const layoutDirty = layoutLoaded && currentLayoutKey !== savedLayoutKey

  useEffect(() => {
    startPolling(intervalMs, overviewPayload)
  }, [startPolling, intervalMs, overviewPayload, payloadKey])

  useEffect(() => {
    if (!hiddenCatalog.length) {
      setSelectedAddId('')
      return
    }

    if (!hiddenCatalog.some(card => card.id === selectedAddId)) setSelectedAddId(hiddenCatalog[0]?.id ?? '')
  }, [hiddenCatalog, selectedAddId])

  const normalizeNextLayout = useCallback((next: DashboardLayoutCard[]) => {
    return normalizeDashboardLayout(next, dashboardCardCatalog, makeDefaultLayout())
  }, [])

  const updateLayout = useCallback((updater: (current: DashboardLayoutCard[]) => DashboardLayoutCard[]) => {
    setSaveNotice(null)
    setLayout(current => normalizeNextLayout(updater(current)))
  }, [normalizeNextLayout])

  const addCard = useCallback((id: string) => {
    const catalogItem = catalogById.get(id)
    if (!catalogItem) return

    updateLayout(current => {
      const nextOrder = visibleDashboardLayoutCards(current).length
      return current.map(card =>
        card.id === id ?
          {
            ...card,
            visible: true,
            order: nextOrder,
            size: catalogItem.defaultSize,
            variant: catalogItem.defaultVariant,
          }
        : card,
      )
    })
  }, [catalogById, updateLayout])

  const removeCard = useCallback((id: string) => {
    updateLayout(current => {
      if (visibleDashboardLayoutCards(current).length <= 1) return current
      return current.map(card => card.id === id ? { ...card, visible: false } : card)
    })
  }, [updateLayout])

  const moveCard = useCallback((id: string, direction: -1 | 1) => {
    updateLayout(current => {
      const visible = visibleDashboardLayoutCards(current)
      const index = visible.findIndex(card => card.id === id)
      const nextIndex = index + direction
      if (index < 0 || nextIndex < 0 || nextIndex >= visible.length) return current

      const reordered = [...visible]
      const [moved] = reordered.splice(index, 1)
      reordered.splice(nextIndex, 0, moved)
      const orderById = new Map(reordered.map((card, order) => [card.id, order]))
      return current.map(card => orderById.has(card.id) ? { ...card, order: orderById.get(card.id) ?? card.order } : card)
    })
  }, [updateLayout])

  const reorderCardBefore = useCallback((dragId: string, targetId: string) => {
    if (dragId === targetId) return

    updateLayout(current => {
      const visible = visibleDashboardLayoutCards(current)
      const from = visible.findIndex(card => card.id === dragId)
      const to = visible.findIndex(card => card.id === targetId)
      if (from < 0 || to < 0) return current

      const reordered = [...visible]
      const [moved] = reordered.splice(from, 1)
      reordered.splice(to, 0, moved)
      const orderById = new Map(reordered.map((card, order) => [card.id, order]))
      return current.map(card => orderById.has(card.id) ? { ...card, order: orderById.get(card.id) ?? card.order } : card)
    })
  }, [updateLayout])

  const startCardDrag = useCallback((id: string, event: React.DragEvent<HTMLElement>) => {
    setDraggedCardId(id)
    setDragOverCardId(null)
    event.dataTransfer.effectAllowed = 'move'
    event.dataTransfer.setData('text/plain', id)
  }, [])

  const dragOverCard = useCallback((id: string, event: React.DragEvent<HTMLDivElement>) => {
    if (!draggedCardId || draggedCardId === id) return
    event.preventDefault()
    event.dataTransfer.dropEffect = 'move'
    setDragOverCardId(id)
  }, [draggedCardId])

  const dropCard = useCallback((id: string, event: React.DragEvent<HTMLDivElement>) => {
    event.preventDefault()
    const dragId = event.dataTransfer.getData('text/plain') || draggedCardId
    if (dragId) reorderCardBefore(dragId, id)
    setDraggedCardId(null)
    setDragOverCardId(null)
  }, [draggedCardId, reorderCardBefore])

  const endCardDrag = useCallback(() => {
    setDraggedCardId(null)
    setDragOverCardId(null)
  }, [])

  const changeCardSize = useCallback((id: string, size: DashboardCardSize) => {
    updateLayout(current => current.map(card => card.id === id ? { ...card, size } : card))
  }, [updateLayout])

  const changeCardVariant = useCallback((id: string, variant: DashboardCardVariant) => {
    updateLayout(current => current.map(card => card.id === id ? { ...card, variant } : card))
  }, [updateLayout])

  const saveLayout = useCallback(async () => {
    const nextLayout = normalizeNextLayout(layout)
    const preferenceLayout = dashboardLayoutPreference(nextLayout)
    const preference = await savePreference({
      preference_key: DASHBOARD_HOME_PREFERENCE_KEY,
      layout: preferenceLayout,
    })
    const saved = preference.layout ?
      normalizeDashboardLayout(preference.layout, dashboardCardCatalog, makeDefaultLayout())
    : nextLayout

    const nextKey = layoutKey(saved)
    setLayout(saved)
    setSavedLayout(saved)
    setSavedLayoutKey(nextKey)
    setNeedsMigrationSave(false)
    setSaveNotice('Layout saved.')
    storeLayout(saved)
    void refreshOverview(buildOverviewPayload(saved))
  }, [layout, normalizeNextLayout, refreshOverview, savePreference])

  const doneCustomizing = useCallback(async () => {
    if (layoutDirty || needsMigrationSave) await saveLayout()
    setCustomizing(false)
  }, [layoutDirty, needsMigrationSave, saveLayout])

  const cancelCustomizing = useCallback(() => {
    setLayout(savedLayout)
    setCustomizing(false)
    setSaveNotice(null)
    void refreshOverview(buildOverviewPayload(savedLayout))
  }, [refreshOverview, savedLayout])

  const resetLayout = useCallback(async () => {
    const defaults = makeDefaultLayout()
    await resetPreference({ preference_key: DASHBOARD_HOME_PREFERENCE_KEY })
    if (typeof window !== 'undefined') window.localStorage.removeItem(DASHBOARD_LAYOUT_STORAGE_KEY)
    setLayout(defaults)
    setSavedLayout(defaults)
    setSavedLayoutKey(layoutKey(defaults))
    setNeedsMigrationSave(false)
    setSaveNotice('Layout reset.')
    void refreshOverview(buildOverviewPayload(defaults))
  }, [refreshOverview, resetPreference])

  const applyPreset = useCallback((preset: DashboardLayoutPreset) => {
    const next = normalizeDashboardLayout(dashboardLayoutFromCards(preset.cards), dashboardCardCatalog, makeDefaultLayout())
    setSelectedPresetId(preset.id)
    setSaveNotice(null)
    setLayout(next)
    void refreshOverview(buildOverviewPayload(next))
  }, [refreshOverview])

  const addSelectedCard = useCallback(() => {
    if (!selectedAddId) return
    addCard(selectedAddId)
  }, [addCard, selectedAddId])

  const overview = wrapper.data
  const tone = dashboardSeverityTone(overview.overall_status)
  const checkedAt = formatCheckedAt(overview.checked_at, wrapper.lastUpdated)
  const cardsById = useMemo(() => new Map(overview.cards.map(card => [card.id, card])), [overview.cards])
  const visibleCards = useMemo(() => {
    return visibleLayout
      .map(layoutCard => {
        const catalogItem = catalogById.get(layoutCard.id)
        if (!catalogItem) return null
        return {
          layoutCard,
          catalogItem,
          card: cardsById.get(layoutCard.id) ?? pendingCardForLayout(layoutCard, catalogItem),
        }
      })
      .filter((item): item is { layoutCard: DashboardLayoutCard; catalogItem: DashboardCardCatalogItem; card: DashboardCardSummary } => Boolean(item))
  }, [cardsById, catalogById, visibleLayout])
  const visibleAttention = useMemo(
    () => overview.attention.filter(issue => visibleIds.has(issue.card_id)),
    [overview.attention, visibleIds],
  )
  const visibleSections = useMemo(
    () => overview.sections.filter(section => visibleSectionIds.has(section.id)),
    [overview.sections, visibleSectionIds],
  )

  return (
    <div className="space-y-6">
      <OverviewShell className={tone.ring}>
        <div className="flex flex-col gap-5 lg:flex-row lg:items-center lg:justify-between">
          <div className="min-w-0">
            <div className="text-xs font-semibold tracking-[0.18em] text-cyan-200/65 uppercase">Admin Dashboard</div>
            <div className="mt-3 flex flex-wrap items-center gap-3">
              <h1 className="text-2xl font-semibold text-white">Health Command Center</h1>
              <DashboardSeverityBadge
                severity={overview.overall_status}
                errorCount={overview.error_count}
                warningCount={overview.warning_count}
                showCount
              />
            </div>
            <p className="mt-2 max-w-3xl text-sm text-white/60">
              Runtime, filesystem, storage, operations, and trend posture at a glance.
            </p>
          </div>

          <div className="flex shrink-0 flex-wrap items-center gap-2">
            <div className={['rounded-2xl border px-3 py-2', tone.border, tone.bg].join(' ')}>
              <div className="text-[11px] text-white/45 uppercase">Warnings</div>
              <div className={['text-lg font-semibold', tone.text].join(' ')}>{overview.warning_count}</div>
            </div>
            <div className={['rounded-2xl border px-3 py-2', tone.border, tone.bg].join(' ')}>
              <div className="text-[11px] text-white/45 uppercase">Errors</div>
              <div className={['text-lg font-semibold', tone.text].join(' ')}>{overview.error_count}</div>
            </div>
            <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2 text-xs text-white/55">
              checked {checkedAt}
            </div>
            <LiveBadge loading={wrapper.loading} error={wrapper.error} lastUpdated={wrapper.lastUpdated} />
          </div>
        </div>

        {wrapper.error ?
          <div className="mt-4 rounded-2xl border border-rose-300/30 bg-rose-400/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        : null}
      </OverviewShell>

      <OverviewShell>
        <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <div className="text-sm font-semibold text-white/90">Home Layout</div>
            <div className="mt-1 text-xs text-white/50">
              Choose server-saved summary cards for this dashboard home. Detail pages stay fixed.
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            {customizing ?
              <>
                <select
                  className="rounded-full border border-white/10 bg-zinc-950/80 px-3 py-1.5 text-xs text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50"
                  value={selectedPresetId}
                  onChange={event => {
                    const preset = dashboardLayoutPresets.find(item => item.id === event.target.value)
                    if (preset) applyPreset(preset)
                  }}>
                  {dashboardLayoutPresets.map(preset => (
                    <option key={preset.id} value={preset.id}>
                      {preset.title}
                    </option>
                  ))}
                </select>
                <select
                  className="rounded-full border border-white/10 bg-zinc-950/80 px-3 py-1.5 text-xs text-white/75 outline-none transition hover:border-cyan-200/30 focus:border-cyan-200/50"
                  value={selectedAddId}
                  onChange={event => setSelectedAddId(event.target.value)}
                  disabled={!hiddenCatalog.length}>
                  {hiddenCatalog.length ?
                    hiddenCatalog.map(card => (
                      <option key={card.id} value={card.id}>
                        {card.title}
                      </option>
                    ))
                  : <option value="">All cards shown</option>}
                </select>
                <button
                  type="button"
                  className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-3 py-1.5 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45 disabled:cursor-not-allowed disabled:opacity-40"
                  onClick={addSelectedCard}
                  disabled={!selectedAddId}>
                  Add Card
                </button>
                <button
                  type="button"
                  className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-xs text-white/70 transition hover:border-amber-200/35 hover:text-amber-100"
                  onClick={() => { void resetLayout().catch(() => undefined) }}
                  disabled={preferenceSaving}>
                  Reset Layout
                </button>
                <button
                  type="button"
                  className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-3 py-1.5 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45 disabled:cursor-not-allowed disabled:opacity-40"
                  onClick={() => { void saveLayout().catch(() => undefined) }}
                  disabled={preferenceSaving || (!layoutDirty && !needsMigrationSave)}>
                  {preferenceSaving ? 'Saving...' : 'Save Layout'}
                </button>
                <button
                  type="button"
                  className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-xs text-white/70 transition hover:border-white/25 hover:text-white"
                  onClick={cancelCustomizing}
                  disabled={preferenceSaving}>
                  Cancel
                </button>
                <button
                  type="button"
                  className="rounded-full border border-emerald-200/25 bg-emerald-400/10 px-3 py-1.5 text-xs font-medium text-emerald-100 transition hover:border-emerald-100/45"
                  onClick={() => { void doneCustomizing().catch(() => undefined) }}
                  disabled={preferenceSaving}>
                  {layoutDirty || needsMigrationSave ? 'Save & Done' : 'Done'}
                </button>
              </>
            : <button
                type="button"
                className="rounded-full border border-cyan-200/25 bg-cyan-400/10 px-3 py-1.5 text-xs font-medium text-cyan-100 transition hover:border-cyan-100/45"
                onClick={() => setCustomizing(true)}>
                Customize
              </button>
            }
          </div>
        </div>
        {customizing ?
          <div className="mt-4 space-y-2">
            <div className="rounded-2xl border border-cyan-300/25 bg-cyan-400/10 px-3 py-2 text-xs text-cyan-100/85">
              Drag cards by their handle or use Up/Down. Changes preview immediately and save to your account when you choose Save Layout or Save & Done.
            </div>
            {needsMigrationSave ?
              <div className="rounded-2xl border border-amber-300/25 bg-amber-400/10 px-3 py-2 text-xs text-amber-100/85">
                A browser-local layout was loaded. Save once to move it to server-backed preferences.
              </div>
            : null}
            {layoutDirty ?
              <div className="rounded-2xl border border-amber-300/25 bg-amber-400/10 px-3 py-2 text-xs text-amber-100/85">
                Unsaved layout changes.
              </div>
            : null}
            {preferenceLoading ?
              <div className="rounded-2xl border border-white/10 bg-white/5 px-3 py-2 text-xs text-white/60">
                Loading saved preferences...
              </div>
            : null}
            {saveNotice ?
              <div className="rounded-2xl border border-emerald-300/25 bg-emerald-400/10 px-3 py-2 text-xs text-emerald-100">
                {saveNotice}
              </div>
            : null}
            {preferenceError ?
              <div className="rounded-2xl border border-rose-300/30 bg-rose-400/10 px-3 py-2 text-xs text-rose-100">
                {preferenceError}
              </div>
            : null}
          </div>
        : null}
      </OverviewShell>

      <OverviewShell>
        <div className="flex items-center justify-between gap-3">
          <div>
            <div className="text-sm font-semibold text-white/90">Attention Queue</div>
            <div className="mt-1 text-xs text-white/50">Current warnings and errors across visible home cards.</div>
          </div>
          <span className="rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-white/55">
            {visibleAttention.length} items
          </span>
        </div>
        <div className="mt-4">
          {visibleAttention.length ?
            <DashboardIssueList issues={visibleAttention} max={6} className="grid grid-cols-1 gap-2 lg:grid-cols-2" />
          : <div className="rounded-2xl border border-emerald-300/25 bg-emerald-400/10 px-3 py-3 text-sm text-emerald-100">
              All monitored systems nominal.
            </div>
          }
        </div>
      </OverviewShell>

      {visibleSections.length ?
        <div className="grid grid-cols-1 gap-4 lg:grid-cols-5">
          {visibleSections.map(section => (
            <SectionCard key={section.id} section={section} />
          ))}
        </div>
      : null}

      <div className="grid grid-cols-1 gap-4 xl:grid-cols-12">
        {visibleCards.map(({ card, layoutCard, catalogItem }, index) => (
          <SummaryCard
            key={layoutCard.id}
            card={card}
            layoutCard={layoutCard}
            catalogItem={catalogItem}
            customizing={customizing}
            index={index}
            total={visibleCards.length}
            onMove={moveCard}
            onDragStart={startCardDrag}
            onDragOverCard={dragOverCard}
            onDropCard={dropCard}
            onDragEnd={endCardDrag}
            dragging={draggedCardId === layoutCard.id}
            dragOver={dragOverCardId === layoutCard.id}
            onRemove={removeCard}
            onSizeChange={changeCardSize}
            onVariantChange={changeCardVariant}
          />
        ))}
      </div>
    </div>
  )
}
