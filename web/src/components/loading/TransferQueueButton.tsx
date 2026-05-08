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

type Placement = 'bottom-right' | 'bottom-left' | 'right'

interface TransferQueueButtonProps {
  className?: string
  compact?: boolean
  placement?: Placement
  showSummaryBar?: boolean
  sidePanel?: boolean
  overlayClassName?: string
}

interface TransferSummary {
  activeCount: number
  queuedCount: number
  runningCount: number
  overallProgress: number
  throughputBytesPerSecond: number
  etaSeconds: number | null
  remainingBytes: number
  status: string
}

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

const formatBytes = (bytes: number) => {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const index = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  const value = bytes / (1024 ** index)
  return `${value.toFixed(value < 10 && index > 0 ? 1 : 0)} ${units[index]}`
}

const formatDuration = (seconds: number | null) => {
  if (!seconds || !Number.isFinite(seconds) || seconds <= 0) return 'Estimating'
  if (seconds < 60) return `${Math.ceil(seconds)}s`
  if (seconds < 3600) return `${Math.ceil(seconds / 60)}m`
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.ceil((seconds % 3600) / 60)
  return `${hours}h ${minutes}m`
}

const visibleTasks = (tasks: TransferTask[], limit: number) => (
  [...tasks].sort((a, b) => {
    if (isActive(a) && !isActive(b)) return -1
    if (!isActive(a) && isActive(b)) return 1
    return b.updatedAt - a.updatedAt
  }).slice(0, limit)
)

const taskTransferredBytes = (task: TransferTask) => {
  if (task.status === 'finalizing' || task.status === 'syncing' || task.status === 'complete') return task.totalBytes ?? task.transferredBytes ?? 0
  return task.transferredBytes ?? 0
}

const summarizeTransfers = (tasks: TransferTask[], now: number): TransferSummary => {
  const activeTasks = tasks.filter(isActive)
  const uploadWork = activeTasks.filter(task => task.type === 'upload')
  const queuedCount = activeTasks.filter(task => task.status === 'queued').length
  const runningCount = activeTasks.length - queuedCount
  const totalBytes = uploadWork.reduce((sum, task) => sum + (task.totalBytes ?? 0), 0)
  const transferredBytes = uploadWork.reduce((sum, task) => sum + taskTransferredBytes(task), 0)
  const averageProgress = activeTasks.length ?
    activeTasks.reduce((sum, task) => sum + Math.max(0, Math.min(100, task.progress)), 0) / activeTasks.length
  : 0
  const overallProgress = totalBytes > 0 ? Math.min(100, Math.max(0, (transferredBytes / totalBytes) * 100)) : averageProgress
  const throughputBytesPerSecond = uploadWork.reduce((sum, task) => {
    if (task.status !== 'uploading' || !task.startedAt || !task.transferredBytes) return sum
    const elapsedSeconds = Math.max(1, (now - task.startedAt) / 1000)
    return sum + (task.transferredBytes / elapsedSeconds)
  }, 0)
  const remainingBytes = Math.max(0, totalBytes - transferredBytes)
  const etaSeconds = throughputBytesPerSecond > 0 && remainingBytes > 0 ? remainingBytes / throughputBytesPerSecond : null

  const status =
    activeTasks.length === 0 ? 'Idle'
    : runningCount > 0 && queuedCount > 0 ? `${runningCount} running, ${queuedCount} queued`
    : runningCount > 0 ? 'Running'
    : 'Waiting to start'

  return {
    activeCount: activeTasks.length,
    queuedCount,
    runningCount,
    overallProgress,
    throughputBytesPerSecond,
    etaSeconds,
    remainingBytes,
    status,
  }
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

const SummaryBar = ({ summary }: { summary: TransferSummary }) => (
  <div className="hidden w-24 shrink-0 items-center gap-2 md:flex lg:w-32">
    <div className="h-1.5 min-w-0 flex-1 overflow-hidden rounded-full bg-white/10">
      <div
        className="h-full rounded-full bg-linear-to-r from-cyan-300 via-sky-300 to-emerald-300 shadow-[0_0_14px_rgba(103,232,249,0.55)] transition-[width]"
        style={{ width: `${Math.max(4, Math.min(100, summary.overallProgress))}%` }}
      />
    </div>
    <span className="w-8 text-right text-[11px] font-medium text-cyan-100/80">{Math.round(summary.overallProgress)}%</span>
  </div>
)

const TaskCard = ({ task }: { task: TransferTask }) => (
  <div className="rounded-lg border border-white/10 bg-white/[0.035] p-2.5">
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
)

const TransferQueueContent = ({
  tasks,
  summary,
  clearTransferTasks,
  onClose,
  panel = false,
  showSummary = false,
}: {
  tasks: TransferTask[]
  summary: TransferSummary
  clearTransferTasks: () => void
  onClose?: () => void
  panel?: boolean
  showSummary?: boolean
}) => {
  const renderedTasks = visibleTasks(tasks, panel ? 14 : 8)
  const hasClearableTasks = renderedTasks.some(task => !isActive(task))

  return (
    <div className={cn('flex min-h-0 flex-col', panel ? 'h-full' : '')}>
      <div className="mb-3 flex items-start justify-between gap-3">
        <div className="min-w-0">
          <div className="text-sm font-semibold text-white">Transfers</div>
          <div className="text-xs text-gray-400">{summary.activeCount > 0 ? summary.status : 'No active work'}</div>
        </div>
        <div className="flex shrink-0 items-center gap-1">
          {hasClearableTasks && (
            <button
              type="button"
              className="flex h-7 w-7 items-center justify-center rounded-md text-gray-300 transition hover:bg-white/10"
              title="Clear completed transfers"
              aria-label="Clear completed transfers"
              onClick={clearTransferTasks}>
              <XmarkIcon className="h-3.5 w-3.5 fill-current" />
            </button>
          )}
          {onClose && (
            <button
              type="button"
              className="flex h-7 w-7 items-center justify-center rounded-md text-gray-300 transition hover:bg-white/10"
              title="Close transfers"
              aria-label="Close transfers"
              onClick={onClose}>
              <XmarkIcon className="h-3.5 w-3.5 fill-current" />
            </button>
          )}
        </div>
      </div>

      {showSummary && (
        <div className="mb-3 rounded-xl border border-white/10 bg-white/[0.04] p-3">
          <div className="mb-2 flex items-center justify-between text-xs">
            <span className="text-gray-400">Queue progress</span>
            <span className="font-medium text-cyan-100">{Math.round(summary.overallProgress)}%</span>
          </div>
          <div className="h-2 overflow-hidden rounded-full bg-white/10">
            <div
              className="h-full rounded-full bg-linear-to-r from-cyan-300 via-sky-300 to-emerald-300 transition-[width]"
              style={{ width: `${summary.activeCount > 0 ? Math.max(4, Math.min(100, summary.overallProgress)) : 0}%` }}
            />
          </div>
          <div className="mt-3 grid grid-cols-3 gap-2 text-xs">
            <div>
              <div className="text-gray-500">Run status</div>
              <div className="mt-0.5 truncate text-gray-100">{summary.status}</div>
            </div>
            <div>
              <div className="text-gray-500">Wait time</div>
              <div className="mt-0.5 text-gray-100">{summary.activeCount > 0 ? formatDuration(summary.etaSeconds) : 'Idle'}</div>
            </div>
            <div>
              <div className="text-gray-500">Throughput</div>
              <div className="mt-0.5 truncate text-gray-100">
                {summary.throughputBytesPerSecond > 0 ? `${formatBytes(summary.throughputBytesPerSecond)}/s` : 'Estimating'}
              </div>
            </div>
          </div>
        </div>
      )}

      {renderedTasks.length === 0 ? (
        <div className="rounded-md border border-white/10 bg-white/[0.03] px-3 py-4 text-center text-xs text-gray-400">
          No transfers yet.
        </div>
      ) : (
        <div className={cn('space-y-2 overflow-y-auto pr-1', panel ? 'min-h-0 flex-1' : 'max-h-96')}>
          {renderedTasks.map(task => <TaskCard key={task.id} task={task} />)}
        </div>
      )}
    </div>
  )
}

export const TransferQueueButton = ({
  className,
  compact = false,
  placement = 'bottom-right',
  showSummaryBar = false,
  sidePanel = false,
  overlayClassName,
}: TransferQueueButtonProps) => {
  const [open, setOpen] = React.useState(false)
  const tasks = useFSStore(state => state.tasks)
  const clearTransferTasks = useFSStore(state => state.clearTransferTasks)
  const activeCount = tasks.filter(isActive).length
  const [now, setNow] = React.useState(() => Date.now())

  React.useEffect(() => {
    if (!activeCount) return
    const timer = window.setInterval(() => setNow(Date.now()), 1000)
    return () => window.clearInterval(timer)
  }, [activeCount])

  const summary = React.useMemo(() => summarizeTransfers(tasks, now), [now, tasks])
  const richQueueDetails = showSummaryBar || sidePanel
  const dropdownPlacement =
    placement === 'right' ? 'left-full top-0 ml-2' : 'right-0 top-full mt-2'
  const title = summary.activeCount > 0 ? `${summary.activeCount} active transfer${summary.activeCount === 1 ? '' : 's'}` : 'Transfers'

  return (
    <div className={cn('relative', compact ? 'w-full' : 'inline-flex shrink-0 items-center gap-2', className)}>
      {showSummaryBar && !compact && summary.activeCount > 0 && <SummaryBar summary={summary} />}

      <button
        type="button"
        title={title}
        aria-label={title}
        className={cn(
          'relative flex items-center justify-center rounded-lg border border-white/10 text-cyan-200 transition hover:bg-white/10',
          compact ? 'h-10 w-full' : 'h-9 w-9 shrink-0',
          open && 'bg-white/10',
        )}
        onClick={() => setOpen(value => !value)}>
        <BarsProgressIcon className="h-4 w-4 fill-current" />
        {summary.activeCount > 0 && (
          <span className="absolute -top-1 -right-1 flex min-h-4 min-w-4 items-center justify-center rounded-full bg-cyan-300 px-1 text-[10px] leading-4 font-semibold text-gray-950">
            {summary.activeCount}
          </span>
        )}
      </button>

      {open && (
        <>
          <div className={cn(
            'absolute z-[80] w-80 max-w-[calc(100vw-1.5rem)] rounded-xl border border-white/10 bg-gray-950/98 p-3 text-sm text-white shadow-2xl shadow-black/50 backdrop-blur-xl',
            dropdownPlacement,
            sidePanel && 'xl:hidden',
            overlayClassName,
          )}>
            <TransferQueueContent
              tasks={tasks}
              summary={summary}
              clearTransferTasks={clearTransferTasks}
              onClose={() => setOpen(false)}
              showSummary={richQueueDetails}
            />
          </div>

          {sidePanel && (
            <aside className={cn(
              'fixed top-0 right-0 z-[80] hidden h-screen w-[26rem] max-w-[calc(100vw-5rem)] border-l border-white/10 bg-gray-950/97 p-4 text-sm text-white shadow-[-30px_0_90px_-40px_rgba(0,0,0,0.9)] backdrop-blur-xl xl:block',
              overlayClassName,
            )}>
              <TransferQueueContent
                tasks={tasks}
                summary={summary}
                clearTransferTasks={clearTransferTasks}
                onClose={() => setOpen(false)}
                panel
                showSummary
              />
            </aside>
          )}
        </>
      )}
    </div>
  )
}

export default TransferQueueButton
