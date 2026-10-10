'use client'

import React from 'react'
import Link from 'next/link'
import { cn } from '@/util/cn'
import { buttonVariants } from '@/components/ui/Button'
import { ArrowRightArrowLeftIcon } from '@/components/ui/icons'
import { toneClasses } from '@/lib/tone'
import { useSyncConflictCount } from '@/features/syncConflicts/summary'

// Top-bar "Sync Conflicts" button with the open count (capped "9+"). Renders nothing while the count is 0, on errors
// and for accounts that can't resolve anything (core counts only vaults with vault.sync.action.resolve_conflicts).
export const SyncConflictsButton = () => {
  const count = useSyncConflictCount()
  if (count <= 0) return null
  const label = `${count} sync conflict${count === 1 ? '' : 's'} need${count === 1 ? 's' : ''} a decision`
  return (
    <Link
      href="/sync-conflicts"
      aria-label={label}
      title={label}
      data-testid="sync-conflicts-button"
      className={cn(buttonVariants({ variant: 'ghost', size: 'icon' }), 'relative')}>
      <ArrowRightArrowLeftIcon aria-hidden />
      <span
        className={cn(
          'tabular text-bg absolute top-1 right-1 grid h-4 min-w-4 place-items-center rounded-full px-1 text-[10px] leading-none font-semibold',
          toneClasses.warn.dot,
        )}>
        {count > 9 ? '9+' : count}
      </span>
    </Link>
  )
}
