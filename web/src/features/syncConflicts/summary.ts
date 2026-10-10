'use client'

import { useWs } from '@/lib/query'
import { isWsError } from '@/lib/ws/errors'
import type { SyncConflictSummary } from '@/models/syncConflicts'

const POLL_MS = 60_000

// Daemons before #187 answer "Unknown command"; a refusal won't change by polling either.
const stopPolling = (error: unknown) =>
  isWsError(error, 'denied', 'unauthorized') || (isWsError(error) && /unknown command/i.test(error.message))

// One poller for the top-bar button, the rail/mobile nav item and the command palette (same query key, so TanStack
// dedupes and ref-counts it; polling pauses in hidden tabs). Core counts only open conflicts in vaults where the
// caller holds vault.sync.action.resolve_conflicts, so a zero total also means "nothing this account can resolve".
export const useSyncConflictSummary = () =>
  useWs('sync.conflicts.summary', null, {
    staleTime: POLL_MS - 5_000,
    retry: false,
    refetchInterval: query => (stopPolling(query.state.error) ? false : POLL_MS),
  })

// The count the shell shows: 0 while loading, on any error, and when nothing is resolvable (the UI then hides).
export const conflictTotal = (data: SyncConflictSummary | undefined, error: unknown) =>
  error || !data ? 0 : Math.max(0, Number(data.total) || 0)

export const useSyncConflictCount = () => {
  const query = useSyncConflictSummary()
  return conflictTotal(query.data, query.error)
}
