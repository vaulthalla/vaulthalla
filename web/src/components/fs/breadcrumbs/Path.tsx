'use client'

import { useFSStore } from '@/stores/fsStore'

export const Path = () => {
  const { path, setPath } = useFSStore()
  const parts = path.split('/').filter(Boolean)

  return (
    parts.length > 0
    && parts.map((part, index) => {
      const fullPath = '/' + parts.slice(0, index + 1).join('/')
      return (
        <div key={index} className="flex items-center gap-1">
          {index === 0 && <span>/</span>}
          <span key={fullPath} className="flex items-center">
            <span className="cursor-pointer text-cyan-400 hover:underline" onClick={() => setPath(fullPath)}>
              {part}
            </span>
            {index < parts.length - 1 && <span className="mx-1">/</span>}
          </span>
        </div>
      )
    })
  )
}
