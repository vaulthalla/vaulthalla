'use client'

import React, { FormEvent, useEffect, useMemo, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import CircleNotchIcon from '@/fa-duotone-regular/circle-notch.svg'
import FolderPlusIcon from '@/fa-duotone-regular/folder-plus.svg'
import XmarkIcon from '@/fa-duotone-regular/xmark.svg'

interface CreateDirectoryModalProps {
  currentPath: string
  onClose: () => void
  onCreate: (name: string) => Promise<void>
}

const displayPath = (value: string) => {
  if (!value) return '/'
  const withSlash = value.startsWith('/') ? value : `/${value}`
  const normalized = withSlash.replace(/\/+/g, '/')
  return normalized.length > 1 ? normalized.replace(/\/+$/g, '') : '/'
}

const cleanDirectoryName = (value: string) => value.trim().replace(/^\/+|\/+$/g, '')

export const CreateDirectoryModal = ({ currentPath, onClose, onCreate }: CreateDirectoryModalProps) => {
  const [name, setName] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [creating, setCreating] = useState(false)
  const inputRef = useRef<HTMLInputElement | null>(null)
  const targetPath = useMemo(() => displayPath(currentPath), [currentPath])

  useEffect(() => {
    inputRef.current?.focus()
  }, [])

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape' && !creating) onClose()
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [creating, onClose])

  const close = () => {
    if (!creating) onClose()
  }

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    const cleanName = cleanDirectoryName(name)
    if (!cleanName) {
      setError('Directory name is required.')
      return
    }
    if (cleanName.includes('/')) {
      setError('Use a single directory name, not a path.')
      return
    }

    setCreating(true)
    setError(null)
    try {
      await onCreate(cleanName)
      onClose()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Unable to create directory.')
      setCreating(false)
    }
  }

  return createPortal(
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 text-white backdrop-blur-sm"
      onMouseDown={close}>
      <form
        className="relative w-full max-w-md overflow-hidden rounded-lg border border-cyan-300/20 bg-[#05080d] shadow-[0_24px_80px_rgba(0,0,0,0.72),0_0_38px_rgba(34,211,238,0.08)]"
        onMouseDown={event => event.stopPropagation()}
        onSubmit={submit}>
        <div className="absolute inset-x-0 top-0 h-px bg-linear-to-r from-transparent via-cyan-300/70 to-transparent" />
        <header className="flex items-start justify-between gap-4 border-b border-white/10 bg-white/[0.03] p-4">
          <div className="min-w-0">
            <div className="mb-2 inline-flex items-center gap-2 rounded-full border border-cyan-300/20 bg-cyan-300/10 px-2.5 py-1 text-[10px] font-bold uppercase tracking-[0.18em] text-cyan-100">
              <FolderPlusIcon className="h-3 w-3 fill-current" />
              Directory
            </div>
            <h2 className="text-xl font-semibold text-white">New Directory</h2>
            <p className="mt-1 truncate font-mono text-xs text-white/45">{targetPath}</p>
          </div>
          <button
            type="button"
            className="rounded-md border border-white/10 bg-white/5 p-2 text-white/70 transition hover:bg-white/10 hover:text-white disabled:cursor-not-allowed disabled:opacity-50"
            onClick={close}
            disabled={creating}
            aria-label="Close create directory dialog">
            <XmarkIcon className="h-4 w-4 fill-current" />
          </button>
        </header>

        <div className="space-y-4 p-4">
          <label className="block text-sm">
            <span className="mb-1.5 block text-xs font-semibold uppercase tracking-[0.14em] text-white/45">Name</span>
            <input
              ref={inputRef}
              className="w-full rounded-md border border-white/10 bg-black/35 px-3 py-2.5 text-white outline-none transition placeholder:text-white/25 focus:border-cyan-300/45 focus:ring-2 focus:ring-cyan-300/10 disabled:cursor-not-allowed disabled:opacity-60"
              value={name}
              onChange={event => {
                setName(event.target.value)
                if (error) setError(null)
              }}
              placeholder="Directory name"
              disabled={creating}
              aria-invalid={Boolean(error)}
            />
          </label>

          {error && (
            <div className="rounded-md border border-red-400/25 bg-red-500/10 px-3 py-2 text-sm text-red-100">
              {error}
            </div>
          )}

          <div className="flex justify-end gap-2 border-t border-white/10 pt-4">
            <button
              type="button"
              className="rounded-md border border-white/10 bg-white/5 px-3 py-2 text-sm font-medium text-white/70 transition hover:bg-white/10 hover:text-white disabled:cursor-not-allowed disabled:opacity-50"
              onClick={close}
              disabled={creating}>
              Cancel
            </button>
            <button
              type="submit"
              className="inline-flex items-center gap-2 rounded-md border border-cyan-300/25 bg-cyan-300/15 px-3 py-2 text-sm font-semibold text-cyan-100 transition hover:bg-cyan-300/20 disabled:cursor-not-allowed disabled:opacity-55"
              disabled={creating}>
              {creating && <CircleNotchIcon className="h-4 w-4 animate-spin fill-current" />}
              Create
            </button>
          </div>
        </div>
      </form>
    </div>,
    document.body,
  )
}
