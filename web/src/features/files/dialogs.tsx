'use client'

import React, { useEffect, useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Dialog, DialogContent } from '@/components/ui/Dialog'
import { Button } from '@/components/ui/Button'
import { Field, Input } from '@/components/ui/Field'
import { InlineError } from '@/components/ui/State'
import { Spinner } from '@/components/ui/Spinner'
import { ChevronRightIcon, FolderIcon, HouseIcon } from '@/components/ui/icons'
import { cn } from '@/util/cn'
import { joinPath, normalizePath, parentOf, pathSegments } from '@/features/files/entries'
import type { FsSource } from '@/features/files/source'

const INVALID_NAME = /[/\\]|^\.{1,2}$/

export const validateName = (name: string) => {
  const trimmed = name.trim()
  if (!trimmed) return 'Enter a name'
  if (INVALID_NAME.test(trimmed)) return 'Names can’t contain slashes or be “.” or “..”'
  if (trimmed.length > 255) return 'That name is too long'
  return null
}

// New folder / rename: one small form.
export const NameDialog = ({
  open,
  onOpenChange,
  title,
  initial = '',
  confirmLabel,
  onSubmit,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  title: string
  initial?: string
  confirmLabel: string
  onSubmit: (name: string) => Promise<void>
}) => {
  const [name, setName] = useState(initial)
  const [error, setError] = useState<unknown>(null)
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    if (open) {
      setName(initial)
      setError(null)
    }
  }, [open, initial])

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    const invalid = validateName(name)
    if (invalid) return setError(new Error(invalid))
    setBusy(true)
    try {
      await onSubmit(name.trim())
      onOpenChange(false)
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent size="sm" title={title}>
        <form onSubmit={submit} className="space-y-4">
          <Field label="Name" htmlFor="fs-name">
            <Input
              id="fs-name"
              autoFocus
              value={name}
              onChange={event => setName(event.target.value)}
              onFocus={event => {
                // Select the stem, not the extension, like a desktop file manager.
                const dot = event.target.value.lastIndexOf('.')
                event.target.setSelectionRange(0, dot > 0 ? dot : event.target.value.length)
              }}
            />
          </Field>
          <InlineError error={error} />
          <div className="flex justify-end gap-2">
            <Button variant="ghost" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button type="submit" variant="primary" loading={busy}>
              {confirmLabel}
            </Button>
          </div>
        </form>
      </DialogContent>
    </Dialog>
  )
}

// Folder picker for move / copy, browsing the same source.
export const DestinationDialog = ({
  open,
  onOpenChange,
  source,
  title,
  confirmLabel,
  startPath,
  disabledPaths,
  onSubmit,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  source: FsSource
  title: string
  confirmLabel: string
  startPath: string
  disabledPaths: string[]
  onSubmit: (dir: string) => Promise<void>
}) => {
  const [dir, setDir] = useState(startPath)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)
  useEffect(() => {
    if (open) {
      setDir(normalizePath(startPath))
      setError(null)
    }
  }, [open, startPath])

  const listing = useQuery({
    queryKey: ['fs', source.key, normalizePath(dir)],
    queryFn: ({ signal }) => source.list(dir, signal),
    enabled: open,
    staleTime: 10_000,
  })
  const folders = (listing.data?.entries ?? []).filter(entry => entry.kind === 'dir')
  const blocked = (path: string) => disabledPaths.some(p => path === p || path.startsWith(`${p}/`))

  const submit = async () => {
    setBusy(true)
    setError(null)
    try {
      await onSubmit(normalizePath(dir))
      onOpenChange(false)
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const segments = pathSegments(dir)

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent
        size="md"
        title={title}
        footer={
          <>
            <Button variant="ghost" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button variant="primary" loading={busy} disabled={blocked(normalizePath(dir))} onClick={submit}>
              {confirmLabel} here
            </Button>
          </>
        }>
        <nav aria-label="Destination" className="mb-3 flex flex-wrap items-center gap-1 text-sm">
          <button type="button" onClick={() => setDir('/')} className="inline-flex items-center gap-1.5 rounded px-1.5 py-0.5 text-fg-muted hover:bg-surface-2 hover:text-fg">
            <HouseIcon className="size-3.5" aria-hidden />
            {source.rootLabel}
          </button>
          {segments.map((segment, index) => (
            <React.Fragment key={index}>
              <ChevronRightIcon className="size-3 text-fg-faint" aria-hidden />
              <button
                type="button"
                onClick={() => setDir(`/${segments.slice(0, index + 1).join('/')}`)}
                className="rounded px-1.5 py-0.5 text-fg-muted hover:bg-surface-2 hover:text-fg">
                {segment}
              </button>
            </React.Fragment>
          ))}
        </nav>
        <div className="max-h-72 min-h-40 overflow-y-auto rounded-card border border-line bg-black/20 p-1">
          {listing.isPending ? (
            <div className="flex justify-center py-10">
              <Spinner />
            </div>
          ) : folders.length === 0 ? (
            <p className="py-10 text-center text-sm text-fg-subtle">No folders here</p>
          ) : (
            folders.map(folder => (
              <button
                key={folder.path}
                type="button"
                disabled={blocked(folder.path)}
                onClick={() => setDir(folder.path)}
                className={cn('flex w-full items-center gap-2.5 rounded-md px-2.5 py-2 text-left text-sm text-fg-muted hover:bg-surface-2 hover:text-fg disabled:opacity-35')}>
                <FolderIcon className="size-4 text-accent-text" aria-hidden />
                <span className="truncate">{folder.name}</span>
              </button>
            ))
          )}
        </div>
        {dir !== '/' ? (
          <button type="button" className="mt-2 text-xs text-fg-subtle hover:text-fg" onClick={() => setDir(parentOf(dir))}>
            ↑ Up one level
          </button>
        ) : null}
        <InlineError error={error} className="mt-3" />
      </DialogContent>
    </Dialog>
  )
}

export const destinationFor = (dir: string, name: string) => joinPath(dir, name)
