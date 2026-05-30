'use client'

import React, { useEffect, useMemo, useRef, useState } from 'react'
import BellIcon from '@/fa-duotone/bell-on.svg'
import CheckIcon from '@/fa-duotone/circle-check.svg'
import RefreshIcon from '@/fa-duotone/arrows-rotate.svg'
import WarningIcon from '@/fa-duotone/triangle-exclamation.svg'
import { PriceNotification } from '@/models/pricing/priceNotification'
import { usePricingStore } from '@/stores/pricingStore'

const severityClass = (severity: PriceNotification['severity']) => {
  switch (severity) {
    case 'critical':
      return 'border-red-500/50 bg-red-500/12 text-red-100'
    case 'error':
      return 'border-rose-500/50 bg-rose-500/12 text-rose-100'
    case 'warning':
      return 'border-amber-400/50 bg-amber-400/12 text-amber-100'
    default:
      return 'border-cyan-400/40 bg-cyan-400/10 text-cyan-100'
  }
}

const formatWhen = (value: number | string | null) => {
  if (value == null) return ''
  const date = new Date(value)
  if (Number.isNaN(date.getTime())) return String(value)
  return date.toLocaleString()
}

export function PricingNotificationCenter() {
  const [open, setOpen] = useState(false)
  const [busyId, setBusyId] = useState<number | null>(null)
  const shellRef = useRef<HTMLDivElement | null>(null)
  const notifications = usePricingStore(state => state.notifications)
  const fetchNotifications = usePricingStore(state => state.fetchNotifications)
  const ackNotification = usePricingStore(state => state.ackNotification)

  useEffect(() => {
    void fetchNotifications({ limit: 50, include_acknowledged: false }).catch(() => undefined)
    const interval = window.setInterval(() => {
      void fetchNotifications({ limit: 50, include_acknowledged: false }).catch(() => undefined)
    }, 30000)
    return () => window.clearInterval(interval)
  }, [fetchNotifications])

  useEffect(() => {
    const onClick = (event: MouseEvent) => {
      if (!shellRef.current?.contains(event.target as Node)) setOpen(false)
    }
    window.addEventListener('mousedown', onClick)
    return () => window.removeEventListener('mousedown', onClick)
  }, [])

  const unreadImportant = useMemo(
    () => notifications.filter(item => !item.isAcknowledged() && (item.severity === 'warning' || item.severity === 'critical')).length,
    [notifications],
  )
  const visible = notifications.slice(0, 8)

  const acknowledge = async (notification: PriceNotification) => {
    setBusyId(notification.id)
    try {
      await ackNotification({ id: notification.id, vault_id: notification.vault_id })
      await fetchNotifications({ limit: 50, include_acknowledged: false }).catch(() => undefined)
    } finally {
      setBusyId(null)
    }
  }

  return (
    <div className="relative" ref={shellRef}>
      <button
        aria-label="Pricing notifications"
        className="relative inline-flex h-10 w-10 items-center justify-center rounded border border-white/10 bg-white/5 text-cyan-100 transition hover:bg-white/10"
        type="button"
        onClick={() => setOpen(value => !value)}>
        <BellIcon className="h-4 w-4 fill-current" />
        {unreadImportant > 0 ?
          <span className="absolute -right-1 -top-1 inline-flex min-w-5 items-center justify-center rounded-full bg-amber-400 px-1 text-[10px] font-semibold text-black">
            {unreadImportant > 99 ? '99+' : unreadImportant}
          </span>
        : null}
      </button>

      {open ?
        <div className="absolute right-0 z-50 mt-2 w-[min(26rem,calc(100vw-2rem))] overflow-hidden rounded border border-white/10 bg-zinc-950 shadow-2xl">
          <div className="flex items-center justify-between border-b border-white/10 px-3 py-2">
            <div className="flex items-center gap-2 text-sm font-semibold text-white">
              <WarningIcon className="h-4 w-4 fill-current text-amber-300" />
              Cost Control
            </div>
            <button
              aria-label="Refresh pricing notifications"
              className="inline-flex h-8 w-8 items-center justify-center rounded border border-white/10 bg-white/5 text-white/70 hover:bg-white/10 hover:text-white"
              type="button"
              onClick={() => void fetchNotifications({ limit: 50, include_acknowledged: false }).catch(() => undefined)}>
              <RefreshIcon className="h-3.5 w-3.5 fill-current" />
            </button>
          </div>

          <div className="max-h-[70vh] overflow-auto">
            {visible.length === 0 ?
              <div className="px-4 py-8 text-center text-sm text-white/50">No active pricing notifications.</div>
            : visible.map(notification => (
                <div key={notification.id} className="border-b border-white/10 px-3 py-3 last:border-b-0">
                  <div className="flex items-start justify-between gap-3">
                    <div className="min-w-0">
                      <span className={`inline-flex rounded border px-2 py-0.5 text-[11px] uppercase tracking-normal ${severityClass(notification.severity)}`}>
                        {notification.severity}
                      </span>
                      <div className="mt-2 truncate text-sm font-medium text-white">{notification.title}</div>
                      <p className="mt-1 line-clamp-2 text-xs leading-5 text-white/60">{notification.message}</p>
                      <div className="mt-2 flex flex-wrap gap-2 text-[11px] text-white/40">
                        {notification.provider_key ? <span>{notification.provider_key}</span> : null}
                        {notification.vault_id ? <span>Vault {notification.vault_id}</span> : null}
                        {notification.policy_id ? <span>Policy {notification.policy_id}</span> : null}
                        <span>{formatWhen(notification.created_at)}</span>
                      </div>
                    </div>

                    <button
                      aria-label="Acknowledge notification"
                      className="inline-flex h-8 w-8 shrink-0 items-center justify-center rounded border border-white/10 bg-white/5 text-white/70 hover:bg-white/10 hover:text-white disabled:opacity-40"
                      disabled={busyId === notification.id}
                      type="button"
                      onClick={() => void acknowledge(notification)}>
                      <CheckIcon className="h-3.5 w-3.5 fill-current" />
                    </button>
                  </div>
                </div>
              ))
            }
          </div>

          <a className="block border-t border-white/10 px-3 py-2 text-center text-xs text-cyan-200 hover:bg-white/5" href="/pricing-budget">
            Open Cost Control
          </a>
        </div>
      : null}
    </div>
  )
}
