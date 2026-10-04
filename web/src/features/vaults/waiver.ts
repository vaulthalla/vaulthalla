'use client'

import { confirm } from '@/components/ui/Confirm'
import { isWsError } from '@/lib/ws/errors'

// The daemon refuses to (re)configure upstream encryption over a bucket that already holds objects until a person
// accepts a waiver (the CLI asks the same question). Show the server's text and resend accepted only on an explicit
// yes; a "no" rethrows the refusal so the caller reports it.
export const withEncryptionWaiver = async <T>(send: (accept: boolean) => Promise<T>): Promise<T> => {
  try {
    return await send(false)
  } catch (error) {
    if (!isWsError(error, 'needs_confirmation') || error.code !== 'encryption_waiver') throw error
    const ok = await confirm({
      title: 'Accept the encryption waiver?',
      description: error.message,
      confirmLabel: 'Accept and continue',
      tone: 'primary',
    })
    if (!ok) throw error
    return send(true)
  }
}
