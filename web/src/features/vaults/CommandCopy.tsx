'use client'

import React, { useState } from 'react'
import { Button } from '@/components/ui/Button'
import { notify } from '@/components/ui/Toast'
import { CheckIcon, CopyIcon } from '@/components/ui/icons'

// A shell command to run on the server (key exports never go through the browser), wrapped in full, with a copy button.
export const CommandCopy = ({ command, label = 'Copy command' }: { command: string; label?: string }) => {
  const [copied, setCopied] = useState(false)
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(command)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    } catch {
      notify.info('Copy it manually', 'Clipboard access isn’t available here.')
    }
  }
  return (
    <div className="mt-2 flex items-start gap-2">
      <code className="border-line text-fg min-w-0 flex-1 rounded-md border bg-black/40 px-2.5 py-2 font-mono text-xs break-all">
        {command}
      </code>
      <Button size="sm" variant="secondary" onClick={copy} aria-label={label}>
        {copied ?
          <CheckIcon aria-hidden />
        : <CopyIcon aria-hidden />}
        {copied ? 'Copied' : 'Copy'}
      </Button>
    </div>
  )
}
