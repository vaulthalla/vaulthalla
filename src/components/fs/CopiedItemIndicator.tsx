'use client'

import React from 'react'
import { useFSStore } from '@/stores/fsStore'

const CopiedItemIndicator = () => {
  const { copiedItem } = useFSStore()

  return (
    copiedItem && (
      <div className="absolute top-4 right-4 rounded bg-blue-900 px-3 py-1 text-sm text-blue-200 shadow">
        Copied: {copiedItem.name}
      </div>
    )
  )
}

export default CopiedItemIndicator
