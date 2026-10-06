import React from 'react'
import { cn } from '@/util/cn'
import { EmptyState } from '@/components/ui/State'
import { Button } from '@/components/ui/Button'
import { DownloadIcon } from '@/components/ui/icons'

// The dark stage every visual renderer draws into.
export const Stage = ({ className, children, ...props }: React.HTMLAttributes<HTMLDivElement>) => (
  <div className={cn('relative grid min-h-64 place-items-center overflow-hidden rounded-card border border-line bg-black/40', className)} {...props}>
    {children}
  </div>
)

// A renderer's "can't show this" state: a reason plus, when allowed, the download escape hatch.
export const Notice = ({
  icon,
  title,
  description,
  onDownload,
  action,
}: {
  icon?: React.ComponentType<React.SVGProps<SVGSVGElement>>
  title: React.ReactNode
  description?: React.ReactNode
  onDownload?: (() => void) | null
  action?: React.ReactNode
}) => (
  <Stage>
    <EmptyState
      icon={icon}
      title={title}
      description={description}
      action={
        onDownload || action ?
          <div className="flex flex-wrap items-center justify-center gap-2">
            {action}
            {onDownload ?
              <Button variant="secondary" onClick={onDownload}>
                <DownloadIcon aria-hidden /> Download
              </Button>
            : null}
          </div>
        : undefined
      }
    />
  </Stage>
)
