'use client'

// Interactive 3D model viewer (GLB, glTF, STL, OBJ) on Babylon.js. This module and ./model/** are the only code that
// imports @babylonjs/*; it must only ever be reached through a dynamic import (next/dynamic, ssr: false), never
// statically, or every route that renders the file browser would ship the engine. `pnpm budgets` enforces both: no
// route's first-load set may contain Babylon, and the lazy chunks carrying MODEL_VIEWER_MARKER have their own budget.

import React, { useEffect, useRef, useState } from 'react'
import { Button } from '@/components/ui/Button'
import { Spinner } from '@/components/ui/Spinner'
import {
  ArrowsRotateIcon,
  ArrowsUpDownLeftRightIcon,
  Grid2Icon,
  LayerGroupIcon,
  RotateLeftIcon,
  TriangleExclamationIcon,
} from '@/components/ui/icons'
import { formatCompact, formatInt } from '@/lib/format'
import { cn } from '@/util/cn'
import { createViewer, ModelWebGLError, type ResolveInit, type Viewer } from './model/viewer'
import { ModelLimitError, ModelUnsupportedError } from './model/scan'

export interface ModelStats {
  meshes: number
  vertices: number
  triangles: number
  materials: number
  bounds: { min: [number, number, number]; max: [number, number, number]; size: [number, number, number] }
}

export interface ModelViewerProps {
  data: ArrayBuffer
  format: 'glb' | 'gltf' | 'stl' | 'obj'
  fileName: string
  /**
   * Fetches a file the model references: glTF external buffers/textures, OBJ material libraries and their textures.
   * Receives the reference as written in the model, percent-decoded, backslashes turned into slashes and a leading
   * `./` removed (so it is relative to the model's folder). Absolute and scheme-qualified references (`https:`,
   * `/x`, `//host`, `blob:`) are never passed and never fetched; `data:` URIs are decoded by the loader itself.
   * `init.signal` is aborted when the viewer closes; a body over `init.maxBytes` (what is left of the model's total
   * budget) must be refused with a RangeError, and `init.onProgress` should see the bytes as they arrive. The engine
   * itself never fetches anything else (see model/urlGate.ts).
   */
  resolveResource?: (uri: string, init: ResolveInit) => Promise<ArrayBuffer>
  onStats?: (stats: ModelStats) => void
  onError?: (error: Error) => void
}

export type { ResolveInit }

/** Stable marker in the viewer's chunk: bin/check-budgets.mjs finds the lazy model-viewer chunks by it. */
export const MODEL_VIEWER_MARKER = 'vh-model-viewer'

type Status =
  | { kind: 'loading' }
  | { kind: 'ready'; stats: ModelStats }
  | { kind: 'error'; message: string; retry: boolean }

const userMessage = (error: Error) => {
  if (error instanceof ModelLimitError || error instanceof ModelUnsupportedError || error instanceof ModelWebGLError)
    return error.message
  const detail = error.message.slice(0, 240)
  return detail ? `This model could not be loaded: ${detail}.`.replace(/\.\.$/, '.') : 'This model could not be loaded.'
}

const length = new Intl.NumberFormat(undefined, { maximumSignificantDigits: 3 })
const plural = (n: number, one: string, many: string) => `${formatInt(n)} ${n === 1 ? one : many}`

export default function ModelViewer({ data, format, fileName, resolveResource, onStats, onError }: ModelViewerProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const viewerRef = useRef<Viewer | null>(null)
  // Callbacks change identity on every parent render; the load effect must not re-run for that.
  const callbacks = useRef({ resolveResource, onStats, onError })
  callbacks.current = { resolveResource, onStats, onError }
  const [status, setStatus] = useState<Status>({ kind: 'loading' })
  const [grid, setGrid] = useState(true)
  const [wireframe, setWireframe] = useState(false)
  const toggles = useRef({ grid, wireframe })
  toggles.current = { grid, wireframe }
  const [attempt, setAttempt] = useState(0)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    let active = true
    setStatus({ kind: 'loading' })
    const fail = (error: unknown, retry: boolean) => {
      if (!active) return
      const err = error instanceof Error ? error : new Error(String(error))
      setStatus({ kind: 'error', message: userMessage(err), retry })
      callbacks.current.onError?.(err)
    }

    let viewer: Viewer
    try {
      viewer = createViewer(canvas, {
        resolveResource:
          callbacks.current.resolveResource ? (uri, init) => callbacks.current.resolveResource!(uri, init) : undefined,
      })
    } catch (error) {
      fail(error, false)
      return () => {
        active = false
      }
    }
    viewerRef.current = viewer
    viewer.onContextLost(() =>
      fail(
        new ModelWebGLError(
          'The graphics context was lost (the GPU may be busy or out of memory). Reload the viewer to try again.',
        ),
        true,
      ),
    )
    viewer.load(data, format, fileName).then(
      stats => {
        if (!active) return
        viewer.setGrid(toggles.current.grid)
        viewer.setWireframe(toggles.current.wireframe)
        setStatus({ kind: 'ready', stats })
        callbacks.current.onStats?.(stats)
      },
      error => fail(error, !(error instanceof ModelLimitError || error instanceof ModelUnsupportedError)),
    )
    return () => {
      active = false
      viewerRef.current = null
      viewer.dispose()
    }
  }, [data, format, fileName, attempt])

  const ready = status.kind === 'ready'
  const toggleGrid = () => {
    viewerRef.current?.setGrid(!grid)
    setGrid(!grid)
  }
  const toggleWireframe = () => {
    viewerRef.current?.setWireframe(!wireframe)
    setWireframe(!wireframe)
  }

  return (
    <div
      data-vh-component={MODEL_VIEWER_MARKER}
      data-testid="model-viewer"
      data-state={status.kind}
      className="rounded-card border-line bg-surface-solid flex h-full w-full flex-col overflow-hidden border">
      <div
        role="toolbar"
        aria-label="3D view"
        className="border-line flex flex-wrap items-center gap-1 border-b px-2 py-1.5">
        <Button
          variant="ghost"
          size="sm"
          disabled={!ready}
          onClick={() => viewerRef.current?.fit()}
          title="Frame the model">
          <ArrowsUpDownLeftRightIcon className="fill-current" aria-hidden /> Fit
        </Button>
        <Button
          variant="ghost"
          size="sm"
          disabled={!ready}
          onClick={() => viewerRef.current?.reset()}
          title="Reset the view">
          <RotateLeftIcon className="fill-current" aria-hidden /> Reset
        </Button>
        <span className="bg-line mx-1 h-4 w-px" aria-hidden />
        <Button
          variant={grid ? 'subtle' : 'ghost'}
          size="sm"
          disabled={!ready}
          aria-pressed={grid}
          onClick={toggleGrid}>
          <Grid2Icon className="fill-current" aria-hidden /> Grid
        </Button>
        <Button
          variant={wireframe ? 'subtle' : 'ghost'}
          size="sm"
          disabled={!ready}
          aria-pressed={wireframe}
          onClick={toggleWireframe}>
          <LayerGroupIcon className="fill-current" aria-hidden /> Wireframe
        </Button>
      </div>

      <div className="relative min-h-72 flex-1">
        <canvas
          // A fresh element per attempt: a canvas whose WebGL context was lost can't create a new one.
          key={attempt}
          ref={canvasRef}
          data-testid="model-viewer-canvas"
          aria-label={`3D view of ${fileName}. Drag to orbit, right-drag or Ctrl-drag to pan, scroll or pinch to zoom.`}
          tabIndex={0}
          // Arrow keys orbit the camera here; keep them from also paging through files in the preview sheet.
          onKeyDown={event => {
            if (event.key.startsWith('Arrow')) event.stopPropagation()
          }}
          className={cn(
            'focus-visible:ring-accent-line absolute inset-0 block size-full touch-none outline-none focus-visible:ring-2',
            status.kind === 'error' && 'invisible',
          )}
        />
        {status.kind === 'loading' ?
          <div className="absolute inset-0 grid place-items-center">
            <Spinner className="size-6" label="Loading model" />
          </div>
        : null}
        {status.kind === 'error' ?
          <div
            role="alert"
            className="absolute inset-0 flex flex-col items-center justify-center gap-3 px-6 text-center">
            <TriangleExclamationIcon className="text-warn size-6 fill-current" aria-hidden />
            <p className="text-fg-muted max-w-md text-sm">{status.message}</p>
            {status.retry ?
              <Button size="sm" onClick={() => setAttempt(n => n + 1)}>
                <ArrowsRotateIcon className="fill-current" aria-hidden /> Reload viewer
              </Button>
            : null}
          </div>
        : null}
      </div>

      <div className="tabular border-line text-fg-subtle flex min-h-8 flex-wrap items-center gap-x-3 gap-y-0.5 border-t px-3 py-1.5 text-xs">
        {ready ?
          <>
            <span>{plural(status.stats.meshes, 'mesh', 'meshes')}</span>
            <span title={formatInt(status.stats.triangles)}>{formatCompact(status.stats.triangles)} triangles</span>
            <span title={formatInt(status.stats.vertices)}>{formatCompact(status.stats.vertices)} vertices</span>
            <span>{plural(status.stats.materials, 'material', 'materials')}</span>
            <span title="Bounding box (model units)">
              {status.stats.bounds.size.map(v => length.format(v)).join(' × ')}
            </span>
          </>
        : <span>{status.kind === 'loading' ? 'Loading…' : format.toUpperCase()}</span>}
      </div>
    </div>
  )
}
