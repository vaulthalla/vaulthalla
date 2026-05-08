'use client'

import React from 'react'
import BarsProgressIcon from '@/fa-duotone-regular/bars-progress.svg'
import CircleCheckIcon from '@/fa-duotone-regular/circle-check.svg'
import CircleNotchIcon from '@/fa-duotone-regular/circle-notch.svg'
import CircleXmarkIcon from '@/fa-duotone-regular/circle-xmark.svg'
import DownloadIcon from '@/fa-duotone-regular/download.svg'
import UploadIcon from '@/fa-duotone-regular/upload.svg'
import XmarkIcon from '@/fa-duotone-regular/xmark.svg'
import { useFSStore } from '@/stores/fsStore'
import type { TransferTask, TransferTaskStatus } from '@/stores/fsStore'
import { cn } from '@/util/cn'

type Placement = 'bottom-right' | 'right'

const activeStatuses: TransferTaskStatus[] = ['queued', 'uploading', 'finalizing', 'syncing']

const isActive = (task: TransferTask) => activeStatuses.includes(task.status)

const statusText = (task: TransferTask) => {
  if (task.status === 'queued') return 'Queued'
  if (task.status === 'uploading') return `Uploading ${Math.round(task.progress)}%`
  if (task.status === 'finalizing') return 'Finalizing'
  if (task.status === 'syncing') return 'Syncing'
  if (task.status === 'complete') return 'Complete'
  if (task.status === 'started') return 'Started in browser'
  return task.error || 'Failed'
}

const StatusIcon = ({ status }: { status: TransferTaskStatus }) => {
  if (status === 'complete' || status === 'started') return <CircleCheckIcon className="h-4 w-4 fill-current text-emerald-300" />
  if (status === 'failed') return <CircleXmarkIcon className="h-4 w-4 fill-current text-red-300" />
  return <CircleNotchIcon className="h-4 w-4 animate-spin fill-current text-cyan-300" />
}

const TypeIcon = ({ type }: { type: TransferTask['type'] }) => (
  type === 'upload' ?
    <UploadIcon className="h-4 w-4 fill-current text-cyan-200" />
  : <DownloadIcon className="h-4 w-4 fill-current text-cyan-200" />
)

export const TransferQueueButton = ({
  className,
  compact = false,
  placement = 'bottom-right',
}: {
  className?: string
  compact?: boolean
  placement?: Placement
}) => {
  const [open, setOpen] = React.useState(false)
  const tasks = useFSStore(state => state.tasks)
  const clearTransferTasks = useFSStore(state => state.clearTransferTasks)

  const activeCount = tasks.filter(isActive).length
  const visibleTasks = React.useMemo(() => (
    [...tasks].sort((a, b) => {
      if (isActive(a) && !isActive(b)) return -1
      if (!isActive(a) && isActive(b)) return 1
      return b.updatedAt - a.updatedAt
    }).slice(0, 8)
  ), [tasks])

  const dropdownPlacement =
    placement === 'right' ? 'left-full top-0 ml-2' : 'right-0 top-full mt-2'
  const title = activeCount > 0 ? `${activeCount} active transfer${activeCount === 1 ? '' : 's'}` : 'Transfers'

  return (
    <div className={cn('relative', compact ? 'w-full' : 'shrink-0', className)}>
      <button
        type="button"
        title={title}
        aria-label={title}
        className={cn(
          'relative flex items-center justify-center rounded-lg border border-white/10 text-cyan-200 transition hover:bg-white/10',
          compact ? 'h-10 w-full' : 'h-9 w-9',
          open && 'bg-white/10',
        )}
        onClick={() => setOpen(value => !value)}>
        <BarsProgressIcon className="h-4 w-4 fill-current" />
        {activeCount > 0 && (
          <span className="absolute -top-1 -right-1 flex min-h-4 min-w-4 items-center justify-center rounded-full bg-cyan-300 px-1 text-[10px] leading-4 font-semibold text-gray-950">
            {activeCount}
          </span>
        )}
      </button>

      {open && (
        <div className={cn(
          'absolute z-50 w-80 max-w-[calc(100vw-1.5rem)] rounded-lg border border-white/10 bg-gray-950 p-3 text-sm text-white shadow-2xl shadow-black/40',
          dropdownPlacement,
        )}>
          <div className="mb-3 flex items-center justify-between gap-3">
            <div>
              <div className="text-sm font-semibold">Transfers</div>
              <div className="text-xs text-gray-400">{activeCount > 0 ? `${activeCount} active` : 'No active work'}</div>
            </div>
            {visibleTasks.some(task => !isActive(task)) && (
              <button
                type="button"
                className="flex h-7 w-7 items-center justify-center rounded-md text-gray-300 transition hover:bg-white/10"
                title="Clear completed transfers"
                aria-label="Clear completed transfers"
                onClick={clearTransferTasks}>
                <XmarkIcon className="h-3.5 w-3.5 fill-current" />
              </button>
            )}
          </div>

          {visibleTasks.length === 0 ? (
            <div className="rounded-md border border-white/10 bg-white/[0.03] px-3 py-4 text-center text-xs text-gray-400">
              No transfers yet.
            </div>
          ) : (
            <div className="max-h-96 space-y-2 overflow-y-auto pr-1">
              {visibleTasks.map(task => (
                <div key={task.id} className="rounded-md border border-white/10 bg-white/[0.03] p-2">
                  <div className="flex items-start gap-2">
                    <div className="mt-0.5 flex shrink-0 items-center gap-1.5">
                      <TypeIcon type={task.type} />
                      <StatusIcon status={task.status} />
                    </div>
                    <div className="min-w-0 flex-1">
                      <div className="truncate text-sm font-medium text-white">{task.label}</div>
                      <div className={cn('mt-0.5 text-xs', task.status === 'failed' ? 'text-red-200' : 'text-gray-400')}>
                        {statusText(task)}
                      </div>
                    </div>
                  </div>

                  {task.type === 'upload' && isActive(task) && (
                    <div className="mt-2 h-1.5 overflow-hidden rounded-full bg-white/10">
                      <div
                        className="h-full rounded-full bg-cyan-300 transition-[width]"
                        style={{ width: `${Math.max(3, Math.min(100, task.progress))}%` }}
                      />
                    </div>
                  )}
                </div>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  )
}

export default TransferQueueButton
