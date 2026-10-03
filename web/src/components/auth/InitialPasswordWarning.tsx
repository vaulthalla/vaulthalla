'use client'

import Link from 'next/link'
import { useState } from 'react'
import { useAuthStore } from '@/stores/authStore'

// Shown to the super admin while its generated initial password is still in use and the plaintext copy is still on
// the server (auth.security.status). Advice only: it never blocks navigation, and either remedy clears it.
export default function InitialPasswordWarning() {
  const file = useAuthStore(state => state.initialPasswordFile)
  const userName = useAuthStore(state => state.user?.name)
  const [dismissed, setDismissed] = useState(false)

  if (!file || dismissed) return null

  return (
    <div
      role="status"
      className="mx-auto my-3 w-full max-w-5xl rounded-2xl border border-amber-400/30 bg-amber-500/10 px-4 py-3 text-sm text-amber-100"
      data-testid="initial-password-warning">
      <div className="flex items-start justify-between gap-4">
        <div className="space-y-1">
          <p className="font-semibold">The initial super-admin password is still stored in plain text on the server.</p>
          <p className="text-amber-100/80">
            It is in <code className="rounded bg-black/30 px-1">{file}</code>. Either{' '}
            {userName ?
              <Link className="underline" href={`/users/${userName}/change-password`}>
                change the password
              </Link>
            : 'change the password'}{' '}
            (or run <code className="rounded bg-black/30 px-1">vh setup set-super-admin-password</code>), or keep it and
            delete that file. Keeping the generated password is fine once the file is gone.
          </p>
        </div>
        <button
          type="button"
          onClick={() => setDismissed(true)}
          className="shrink-0 rounded-md border border-white/10 bg-white/5 px-2 py-1 text-xs text-white/70 hover:bg-white/10"
          aria-label="Hide this warning until the next page load">
          Hide
        </button>
      </div>
    </div>
  )
}
