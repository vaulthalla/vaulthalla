'use client'

import React, { useState } from 'react'
import { Button } from '@/components/ui/Button'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Segmented } from '@/components/ui/Tabs'
import { CATALOG, visibleCards, type CardVariant, type LayoutCard } from '@/features/health/catalog'

// Loaded on demand from the overview (next/dynamic): the dialog stack isn't needed for first paint.
export const AddCardDialog = ({
  open,
  onOpenChange,
  layout,
  onAdd,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  layout: LayoutCard[]
  onAdd: (id: string, variant: CardVariant) => void
}) => {
  const [style, setStyle] = useState<CardVariant>('visual')
  const present = new Set(visibleCards(layout).map(c => `${c.id}:${c.variant}`))
  const sections = [...new Set(CATALOG.map(c => c.section))]
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent title="Add a card" description="Cards show the daemon's own summary of each subsystem." size="lg">
        <div className="mb-4">
          <Segmented
            label="Card style"
            value={style}
            onChange={setStyle}
            options={[
              { value: 'visual', label: 'Chart' },
              { value: 'tiles', label: 'Numbers' },
            ]}
          />
        </div>
        <div className="max-h-[60vh] space-y-5 overflow-y-auto pr-1">
          {sections.map(section => (
            <div key={section}>
              <h3 className="mb-2 text-xs font-medium tracking-wide text-fg-subtle uppercase">{section}</h3>
              <ul className="divide-y divide-line rounded-card border border-line">
                {CATALOG.filter(c => c.section === section).map(item => {
                  const variant = item.supportedVariants.includes(style) ? style : item.defaultVariant
                  const added = present.has(`${item.id}:${variant}`)
                  return (
                    <li key={item.id} className="flex items-center gap-3 px-3.5 py-2.5">
                      <div className="min-w-0 flex-1">
                        <div className="text-sm font-medium text-fg">{item.title}</div>
                        <div className="truncate text-xs text-fg-subtle">{item.description}</div>
                      </div>
                      <span className="hidden text-xs text-fg-faint sm:inline">{variant === 'visual' ? 'Chart' : 'Numbers'}</span>
                      <Button size="sm" variant="secondary" disabled={added} onClick={() => onAdd(item.id, variant)} aria-label={`Add ${item.title}`}>
                        {added ? 'Added' : 'Add'}
                      </Button>
                    </li>
                  )
                })}
              </ul>
            </div>
          ))}
        </div>
      </DialogContent>
    </Dialog>
  )
}

export default AddCardDialog
