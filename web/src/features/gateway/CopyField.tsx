'use client'

import React, { useState } from 'react'
import { Button } from '@/components/ui/Button'
import { notify } from '@/components/ui/Toast'
import { CheckIcon, CopyIcon } from '@/components/ui/icons'
import { cn } from '@/util/cn'

export const CopyField = ({ label, value, secret = false }: { label: string; value: string; secret?: boolean }) => {
  const [copied, setCopied] = useState(false)
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(value)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    } catch {
      notify.info('Copy it manually', 'Clipboard access isn’t available here.')
    }
  }
  return (
    <div>
      <div className="text-fg-subtle mb-1 text-xs font-medium">{label}</div>
      <div className="flex items-center gap-2">
        <code
          className={cn(
            'border-line text-fg min-w-0 flex-1 truncate rounded-md border bg-black/40 px-2.5 py-2 font-mono text-xs',
            secret && 'text-accent-text',
          )}
          title={value}>
          {value}
        </code>
        <Button size="sm" variant="secondary" onClick={copy} aria-label={`Copy ${label}`}>
          {copied ?
            <CheckIcon aria-hidden />
          : <CopyIcon aria-hidden />}
          {copied ? 'Copied' : 'Copy'}
        </Button>
      </div>
    </div>
  )
}
