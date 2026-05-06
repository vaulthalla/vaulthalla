'use client'

import { useRouter } from 'next/navigation'
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react'

import {
  DASHBOARD_LAYOUT_STORAGE_KEY,
  dashboardLayoutInstanceId,
  dashboardLayoutPreference,
  type DashboardCardSize,
  type DashboardCardVariant,
  type DashboardLayoutCard,
  normalizeDashboardLayout,
  visibleDashboardLayoutCards,
} from '@/models/dashboard/dashboardLayout'
import { DASHBOARD_HOME_PREFERENCE_KEY } from '@/models/dashboard/dashboardPreferences'
import type { DashboardMetricSummary } from '@/models/stats/dashboardOverview'
import {
  dashboardCardCatalog,
  dashboardCardCatalogById,
  dashboardLayoutFromCards,
  type DashboardLayoutPreset,
} from '@/components/dashboard/dashboardCardCatalog'
import { dedupeDashboardIssues } from '@/components/dashboard/DashboardIssueList'
import { dashboardSeverityTone, sortDashboardIssues } from '@/components/dashboard/dashboardSeverity'
import { dashboardMetricByKey } from '@/components/dashboard/dashboardMetricCuration'
import { DashboardAttentionStrip } from '@/components/dashboard/overview/DashboardAttentionStrip'
import { DashboardCommandBar } from '@/components/dashboard/overview/DashboardCommandBar'
import { DashboardGrid, type DashboardVisibleCard } from '@/components/dashboard/overview/DashboardGrid'
import { OverviewShell } from '@/components/dashboard/overview/OverviewShell'
import { formatCheckedAt } from '@/components/dashboard/overview/lib/formatters'
import {
  layoutKey,
  loadStoredLayout,
  makeDefaultLayout,
  storeLayout,
} from '@/components/dashboard/overview/lib/layoutStorage'
import { buildOverviewPayload, SYSTEM_HEALTH_CARD_ID } from '@/components/dashboard/overview/lib/overviewPayload'
import { reorderLayoutBefore } from '@/components/dashboard/overview/lib/dragReorder'
import { pendingCardForLayout } from '@/components/dashboard/overview/lib/pendingCard'
import { useDashboardPreferencesStore } from '@/stores/dashboardPreferencesStore'
import { useStatsStore } from '@/stores/statsStore'

export default function DashboardOverviewComponent({ intervalMs = 7500 }: { intervalMs?: number }) {
  const router = useRouter()
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
  const [cardPickerOpen, setCardPickerOpen] = useState(false)
  const [pickerSize, setPickerSize] = useState<DashboardCardSize>('2x1')
  const [pickerVariant, setPickerVariant] = useState<DashboardCardVariant>('visual')
  const [selectedPresetId, setSelectedPresetId] = useState('default')
  const [draggedCardId, setDraggedCardId] = useState<string | null>(null)
  const [dragOverCardId, setDragOverCardId] = useState<string | null>(null)
  const dragJustEndedRef = useRef(false)

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
  const visibleCombos = useMemo(() => new Set(visibleLayout.map(card => `${card.id}:${card.variant}`)), [visibleLayout])
  const addableCatalog = useMemo(
    () => dashboardCardCatalog.filter(card => card.available && card.supportedVariants.some(variant => !visibleCombos.has(`${card.id}:${variant}`))),
    [visibleCombos],
  )
  const overviewPayload = useMemo(() => buildOverviewPayload(layout), [layout])
  const payloadKey = useMemo(() => JSON.stringify(overviewPayload.cards), [overviewPayload.cards])
  const currentLayoutKey = useMemo(() => layoutKey(layout), [layout])
  const layoutDirty = layoutLoaded && currentLayoutKey !== savedLayoutKey

  useEffect(() => {
    startPolling(intervalMs, overviewPayload)
  }, [startPolling, intervalMs, overviewPayload, payloadKey])

  const normalizeNextLayout = useCallback((next: DashboardLayoutCard[]) => {
    return normalizeDashboardLayout(next, dashboardCardCatalog, makeDefaultLayout())
  }, [])

  const updateLayout = useCallback((updater: (current: DashboardLayoutCard[]) => DashboardLayoutCard[]) => {
    setSaveNotice(null)
    setLayout(current => normalizeNextLayout(updater(current)))
  }, [normalizeNextLayout])

  const addCard = useCallback((id: string, size?: DashboardCardSize, variant?: DashboardCardVariant) => {
    const catalogItem = catalogById.get(id)
    if (!catalogItem) return
    const nextVariant = variant && catalogItem.supportedVariants.includes(variant) ? variant : catalogItem.defaultVariant
    const supportedSizes = catalogItem.variantSupportedSizes?.[nextVariant] ?? catalogItem.supportedSizes
    const nextSize =
      size && supportedSizes.includes(size) ? size
      : supportedSizes.includes(catalogItem.defaultSize) ? catalogItem.defaultSize
      : supportedSizes[0] ?? catalogItem.defaultSize
    const nextInstanceId = dashboardLayoutInstanceId(id, nextVariant)

    updateLayout(current => {
      if (visibleDashboardLayoutCards(current).some(card => card.id === id && card.variant === nextVariant)) return current
      const nextOrder = visibleDashboardLayoutCards(current).length
      return [
        ...current,
        {
          instanceId: nextInstanceId,
          id,
          visible: true,
          order: nextOrder,
          size: nextSize,
          variant: nextVariant,
        },
      ]
    })
    setCardPickerOpen(false)
  }, [catalogById, updateLayout])

  const removeCard = useCallback((instanceId: string) => {
    updateLayout(current => {
      if (visibleDashboardLayoutCards(current).length <= 1) return current
      return current.filter(card => card.instanceId !== instanceId)
    })
  }, [updateLayout])

  const moveCard = useCallback((instanceId: string, direction: -1 | 1) => {
    updateLayout(current => {
      const visible = visibleDashboardLayoutCards(current)
      const index = visible.findIndex(card => card.instanceId === instanceId)
      const nextIndex = index + direction
      if (index < 0 || nextIndex < 0 || nextIndex >= visible.length) return current

      const reordered = [...visible]
      const [moved] = reordered.splice(index, 1)
      reordered.splice(nextIndex, 0, moved)
      const orderByInstanceId = new Map(reordered.map((card, order) => [card.instanceId, order]))
      return current.map(card => orderByInstanceId.has(card.instanceId) ? { ...card, order: orderByInstanceId.get(card.instanceId) ?? card.order } : card)
    })
  }, [updateLayout])

  const reorderCardBefore = useCallback((dragId: string, targetId: string) => {
    updateLayout(current => reorderLayoutBefore(current, dragId, targetId))
  }, [updateLayout])

  const persistLayout = useCallback(async (nextLayout: DashboardLayoutCard[], notice = 'Layout saved.') => {
    const normalized = normalizeNextLayout(nextLayout)
    const preferenceLayout = dashboardLayoutPreference(normalized)
    const preference = await savePreference({
      preference_key: DASHBOARD_HOME_PREFERENCE_KEY,
      layout: preferenceLayout,
    })
    const saved = preference.layout ?
      normalizeDashboardLayout(preference.layout, dashboardCardCatalog, makeDefaultLayout())
    : normalized

    const nextKey = layoutKey(saved)
    setLayout(saved)
    setSavedLayout(saved)
    setSavedLayoutKey(nextKey)
    setNeedsMigrationSave(false)
    setSaveNotice(notice)
    storeLayout(saved)
    void refreshOverview(buildOverviewPayload(saved))
  }, [normalizeNextLayout, refreshOverview, savePreference])

  const startCardDrag = useCallback((id: string, event: React.DragEvent<HTMLElement>) => {
    dragJustEndedRef.current = true
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
    if (dragId) {
      if (customizing) {
        reorderCardBefore(dragId, id)
      } else {
        const nextLayout = normalizeNextLayout(reorderLayoutBefore(layout, dragId, id))
        setSaveNotice(null)
        setLayout(nextLayout)
        void persistLayout(nextLayout, 'Order saved.').catch(() => undefined)
      }
    }
    setDraggedCardId(null)
    setDragOverCardId(null)
    window.setTimeout(() => {
      dragJustEndedRef.current = false
    }, 250)
  }, [customizing, draggedCardId, layout, normalizeNextLayout, persistLayout, reorderCardBefore])

  const endCardDrag = useCallback(() => {
    setDraggedCardId(null)
    setDragOverCardId(null)
    window.setTimeout(() => {
      dragJustEndedRef.current = false
    }, 250)
  }, [])

  const changeCardSize = useCallback((instanceId: string, size: DashboardCardSize) => {
    updateLayout(current => current.map(card => card.instanceId === instanceId ? { ...card, size } : card))
  }, [updateLayout])

  const changeCardVariant = useCallback((instanceId: string, variant: DashboardCardVariant) => {
    updateLayout(current => current.map(card => {
      if (card.instanceId !== instanceId) return card
      if (current.some(other => other.instanceId !== instanceId && other.visible && other.id === card.id && other.variant === variant)) return card
      return {
        ...card,
        instanceId: dashboardLayoutInstanceId(card.id, variant),
        variant,
      }
    }))
  }, [updateLayout])

  const saveLayout = useCallback(async () => {
    await persistLayout(layout)
  }, [layout, persistLayout])

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

  const openCard = useCallback((href: string) => {
    if (dragJustEndedRef.current) return
    router.push(href || '/dashboard')
  }, [router])

  const overview = wrapper.data
  const tone = dashboardSeverityTone(overview.overall_status)
  const checkedAt = formatCheckedAt(overview.checked_at, wrapper.lastUpdated)
  const cardsById = useMemo(() => new Map(overview.cards.map(card => [card.id, card])), [overview.cards])
  const requestedCardSummaries = useMemo(() => overview.cards.filter(card => card.id !== SYSTEM_HEALTH_CARD_ID), [overview.cards])
  const healthCard = cardsById.get(SYSTEM_HEALTH_CARD_ID)
  const healthMetricMap = useMemo(() => dashboardMetricByKey(healthCard?.metrics ?? []), [healthCard])
  const commandHealthMetrics = useMemo(
    () => ['services', 'protocols', 'deps']
      .map(key => healthMetricMap.get(key))
      .filter((metric): metric is DashboardMetricSummary => Boolean(metric)),
    [healthMetricMap],
  )
  const shellAdminMetric = healthMetricMap.get('shell_admin_uid')
  const shellSetupAdvisory = shellAdminMetric?.value === 'setup' ? shellAdminMetric : null
  const visibleCards = useMemo<DashboardVisibleCard[]>(() => {
    return visibleLayout
      .map((layoutCard, index) => {
        const catalogItem = catalogById.get(layoutCard.id)
        if (!catalogItem) return null
        const sequencedCard = requestedCardSummaries[index]
        const matchedCard = sequencedCard?.id === layoutCard.id && sequencedCard.variant === layoutCard.variant ?
          sequencedCard
        : requestedCardSummaries.find(card => card.id === layoutCard.id && card.variant === layoutCard.variant)
        return {
          layoutCard,
          catalogItem,
          card: matchedCard ?? pendingCardForLayout(layoutCard, catalogItem),
        }
      })
      .filter((item): item is DashboardVisibleCard => Boolean(item))
  }, [catalogById, requestedCardSummaries, visibleLayout])
  const visibleAttention = useMemo(
    () => dedupeDashboardIssues(
      overview.attention.filter(issue => visibleIds.has(issue.card_id) || issue.card_id === SYSTEM_HEALTH_CARD_ID),
    ),
    [overview.attention, visibleIds],
  )
  const visibleAttentionPreview = useMemo(() => sortDashboardIssues(visibleAttention).slice(0, 3), [visibleAttention])
  const hiddenAttentionCount = Math.max(0, visibleAttention.length - visibleAttentionPreview.length)

  return (
    <div className="space-y-4">
      <OverviewShell className={tone.ring}>
        <DashboardCommandBar
          overallStatus={overview.overall_status}
          errorCount={overview.error_count}
          warningCount={overview.warning_count}
          loading={wrapper.loading || preferenceLoading}
          error={wrapper.error || preferenceError}
          lastUpdated={wrapper.lastUpdated}
          checkedAt={checkedAt}
          commandHealthMetrics={commandHealthMetrics}
          shellSetupAdvisory={shellSetupAdvisory}
          customizing={customizing}
          selectedPresetId={selectedPresetId}
          cardPickerOpen={cardPickerOpen}
          addableCatalog={addableCatalog}
          visibleCombos={visibleCombos}
          pickerSize={pickerSize}
          pickerVariant={pickerVariant}
          preferenceSaving={preferenceSaving}
          layoutDirty={layoutDirty}
          needsMigrationSave={needsMigrationSave}
          onCustomize={() => setCustomizing(true)}
          onPresetChange={applyPreset}
          onToggleCardPicker={() => setCardPickerOpen(open => !open)}
          onPickerSizeChange={setPickerSize}
          onPickerVariantChange={setPickerVariant}
          onAddCard={addCard}
          onReset={() => { void resetLayout().catch(() => undefined) }}
          onSave={() => { void saveLayout().catch(() => undefined) }}
          onCancel={cancelCustomizing}
          onDone={() => { void doneCustomizing().catch(() => undefined) }}
        />

        <DashboardAttentionStrip issues={visibleAttentionPreview} hiddenCount={hiddenAttentionCount} />

        {wrapper.error ?
          <div className="mt-3 rounded-2xl border border-rose-300/30 bg-rose-400/10 px-3 py-2 text-sm text-rose-100">
            {wrapper.error}
          </div>
        : null}

        {customizing ?
          <div className="mt-3 flex flex-wrap gap-2">
            <div className="rounded-full border border-cyan-300/25 bg-cyan-400/10 px-3 py-1.5 text-xs text-cyan-100/85">
              Drag handles or use Up/Down. Save stores this layout to your account.
            </div>
            {needsMigrationSave ?
              <div className="rounded-full border border-amber-300/25 bg-amber-400/10 px-3 py-1.5 text-xs text-amber-100/85">
                Browser-local layout loaded; save once to sync it.
              </div>
            : null}
            {layoutDirty ?
              <div className="rounded-full border border-amber-300/25 bg-amber-400/10 px-3 py-1.5 text-xs text-amber-100/85">
                Unsaved layout changes.
              </div>
            : null}
            {preferenceLoading ?
              <div className="rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-xs text-white/60">
                Loading saved preferences...
              </div>
            : null}
            {saveNotice ?
              <div className="rounded-full border border-emerald-300/25 bg-emerald-400/10 px-3 py-1.5 text-xs text-emerald-100">
                {saveNotice}
              </div>
            : null}
            {preferenceError ?
              <div className="rounded-full border border-rose-300/30 bg-rose-400/10 px-3 py-1.5 text-xs text-rose-100">
                {preferenceError}
              </div>
            : null}
          </div>
        : null}
      </OverviewShell>

      <DashboardGrid
        visibleCards={visibleCards}
        customizing={customizing}
        draggedCardId={draggedCardId}
        dragOverCardId={dragOverCardId}
        onMove={moveCard}
        onDragStart={startCardDrag}
        onDragOverCard={dragOverCard}
        onDropCard={dropCard}
        onDragEnd={endCardDrag}
        onOpen={openCard}
        onRemove={removeCard}
        onSizeChange={changeCardSize}
        onVariantChange={changeCardVariant}
      />
    </div>
  )
}
