'use client'

import React, { FormEvent, useEffect, useState } from 'react'
import { createPortal } from 'react-dom'
import XmarkIcon from '@/fa-duotone-regular/xmark.svg'
import CircleNotchIcon from '@/fa-duotone-regular/circle-notch.svg'
import { User } from '@/models/user'
import { useAuthStore } from '@/stores/authStore'

// The same question the CLI asks (`vh user delete`): the account's vaults go with it unless they are transferred.
export const USER_DELETE_CONFIRMATION =
  "Are you sure you wish to delete this user? The user's existing vaults will be destroyed unless ownership is transferred."

interface DeleteUserModalProps {
  user: User
  onClose: () => void
  onDeleted: () => void
}

export const DeleteUserModal = ({ user, onClose, onDeleted }: DeleteUserModalProps) => {
  const [heirs, setHeirs] = useState<User[]>([])
  const [transferTo, setTransferTo] = useState<string>('')
  const [error, setError] = useState<string | null>(null)
  const [deleting, setDeleting] = useState(false)

  useEffect(() => {
    useAuthStore
      .getState()
      .getUsers()
      .then(users => setHeirs(users.filter(candidate => candidate.id !== user.id && candidate.is_active)))
      .catch(() => setHeirs([]))
  }, [user.id])

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && !deleting) onClose()
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [deleting, onClose])

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    setDeleting(true)
    setError(null)
    try {
      await useAuthStore
        .getState()
        .deleteUser({ id: user.id, confirm: true, ...(transferTo ? { transfer_to: Number(transferTo) } : {}) })
      onDeleted()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Unable to delete the user.')
      setDeleting(false)
    }
  }

  return createPortal(
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 text-white backdrop-blur-sm"
      onMouseDown={() => !deleting && onClose()}>
      <form
        className="relative w-full max-w-md overflow-hidden rounded-lg border border-red-400/25 bg-[#05080d] shadow-[0_24px_80px_rgba(0,0,0,0.72)]"
        onMouseDown={event => event.stopPropagation()}
        onSubmit={submit}
        data-testid="delete-user-modal">
        <header className="flex items-start justify-between gap-4 border-b border-white/10 bg-white/[0.03] p-4">
          <div className="min-w-0">
            <h2 className="text-xl font-semibold text-white">Delete {user.name}</h2>
          </div>
          <button
            type="button"
            className="rounded-md border border-white/10 bg-white/5 p-2 text-white/70 transition hover:bg-white/10 hover:text-white disabled:opacity-50"
            onClick={onClose}
            disabled={deleting}
            aria-label="Close delete user dialog">
            <XmarkIcon className="h-4 w-4 fill-current" />
          </button>
        </header>

        <div className="space-y-4 p-4">
          <p className="text-sm text-white/80">{USER_DELETE_CONFIRMATION}</p>
          <label className="block text-sm">
            <span className="mb-1.5 block text-xs font-semibold uppercase tracking-[0.14em] text-white/45">
              Their vaults
            </span>
            <select
              className="w-full rounded-md border border-white/10 bg-black/35 px-3 py-2.5 text-white"
              value={transferTo}
              onChange={event => setTransferTo(event.target.value)}
              disabled={deleting}
              data-testid="delete-user-transfer-select">
              <option value="">Destroy them</option>
              {heirs.map(heir => (
                <option key={heir.id} value={String(heir.id)}>
                  Transfer to {heir.name}
                </option>
              ))}
            </select>
          </label>
          {error && <p className="text-sm text-red-300">{error}</p>}
        </div>

        <footer className="flex justify-end gap-2 border-t border-white/10 p-4">
          <button
            type="button"
            className="rounded-md border border-white/10 bg-white/5 px-4 py-2 text-sm text-white/80 hover:bg-white/10"
            onClick={onClose}
            disabled={deleting}>
            Cancel
          </button>
          <button
            type="submit"
            className="inline-flex items-center gap-2 rounded-md bg-red-600 px-4 py-2 text-sm font-semibold text-white hover:bg-red-500 disabled:opacity-60"
            disabled={deleting}
            data-testid="delete-user-confirm">
            {deleting && <CircleNotchIcon className="h-4 w-4 animate-spin fill-current" />}
            Delete user
          </button>
        </footer>
      </form>
    </div>,
    document.body,
  )
}

export default DeleteUserModal
