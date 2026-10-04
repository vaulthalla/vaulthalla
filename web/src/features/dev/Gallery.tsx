'use client'

import dynamic from 'next/dynamic'

// Loaded on demand so the gallery never counts against a production route's first-load budget.
export const Gallery = dynamic(() => import('@/features/dev/UiGallery').then(m => m.UiGallery), { ssr: false })
