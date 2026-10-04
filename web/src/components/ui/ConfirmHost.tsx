'use client'

import React, { useState } from 'react'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Input } from '@/components/ui/Field'
import { useConfirmStore } from '@/components/ui/Confirm'

export const ConfirmHost = () => {
  const request = useConfirmStore(state => state.request)
  const [typed, setTyped] = useState('')

  const close = (ok: boolean) => {
    request?.resolve(ok)
    useConfirmStore.setState({ request: null })
    setTyped('')
  }

  const blocked = Boolean(request?.typeToConfirm) && typed !== request?.typeToConfirm

  return (
    <Dialog open={Boolean(request)} onOpenChange={open => !open && close(false)}>
      {request ? (
        <DialogContent
          size="sm"
          title={request.title}
          description={typeof request.description === 'string' ? request.description : undefined}
          footer={
            <>
              <Button variant="ghost" onClick={() => close(false)}>
                {request.cancelLabel ?? 'Cancel'}
              </Button>
              <Button
                variant={request.tone === 'primary' ? 'primary' : 'danger-solid'}
                disabled={blocked}
                onClick={() => close(true)}
                autoFocus={!request.typeToConfirm}>
                {request.confirmLabel ?? 'Confirm'}
              </Button>
            </>
          }>
          {typeof request.description === 'string' ? null : <div className="text-sm text-fg-muted">{request.description}</div>}
          {request.typeToConfirm ? (
            <div className="mt-1 space-y-2 text-sm text-fg-muted">
              <p>
                Type <span className="font-mono text-fg">{request.typeToConfirm}</span> to confirm.
              </p>
              <Input
                autoFocus
                value={typed}
                onChange={event => setTyped(event.target.value)}
                aria-label={`Type ${request.typeToConfirm} to confirm`}
                onKeyDown={event => {
                  if (event.key === 'Enter' && !blocked) close(true)
                }}
              />
            </div>
          ) : null}
        </DialogContent>
      ) : null}
    </Dialog>
  )
}
