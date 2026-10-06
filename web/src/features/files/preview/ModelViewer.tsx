'use client'

// PLACEHOLDER (WEB track): the real 3D viewer is owned by the WEB-3D track and replaces this file at integration.
// It exists only so the WEB track type-checks and builds; it imports no engine. The contract ModelRenderer relies on:
// default export, props exactly as below.

import React, { useEffect } from 'react'

export interface ModelViewerProps {
  data: ArrayBuffer
  format: 'glb' | 'gltf' | 'stl' | 'obj'
  fileName: string
  resolveResource?: (uri: string) => Promise<ArrayBuffer>
  onStats?: (stats: unknown) => void
  onError?: (error: unknown) => void
}

export default function ModelViewer({ data, format, onStats }: ModelViewerProps) {
  useEffect(() => onStats?.({ bytes: data.byteLength }), [data, onStats])
  return (
    <div className="grid size-full place-items-center text-sm text-fg-subtle">
      3D viewer placeholder · {format.toUpperCase()} · {data.byteLength.toLocaleString()} bytes
    </div>
  )
}
