'use client'

import React from 'react'
import dynamic from 'next/dynamic'
import { Spinner } from '@/components/ui/Spinner'
import { Stage } from '@/features/files/preview/Frame'
import { ImageRenderer, NativeImageRenderer } from '@/features/files/preview/ImageRenderers'
import type { RendererProps } from '@/features/files/preview/types'

// plan.renderer → component. The sheet is itself a lazy chunk; every heavy renderer is a further nested chunk, so
// opening an image never downloads the PDF pager, media player, text editor or the 3D engine (which ModelRenderer
// loads lazily again, only once the model bytes are in hand).
const loading = () => (
  <Stage>
    <Spinner className="size-6" label="Loading viewer" />
  </Stage>
)

const PdfRenderer = dynamic(() => import('@/features/files/preview/PdfRenderer'), { ssr: false, loading })
const ModelRenderer = dynamic(() => import('@/features/files/preview/ModelRenderer'), { ssr: false, loading })

type Renderer = React.ComponentType<RendererProps>

const RENDERERS: Record<string, Renderer> = {
  image: ImageRenderer,
  svg: NativeImageRenderer,
  'image-native': NativeImageRenderer,
  pdf: PdfRenderer,
}

export const rendererFor = (name: string): Renderer | null => {
  if (name.startsWith('model:') || name === 'derived:step-glb') return ModelRenderer
  return RENDERERS[name] ?? null
}

// Wider sheet for renderers that benefit from the room.
export const wideRenderer = (name: string) => !['image', 'svg', 'image-native', 'audio', 'none'].includes(name)
