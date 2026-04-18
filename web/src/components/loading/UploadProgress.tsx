'use client'

import CircleNotch from '@/fa-duotone/circle-notch.svg'
import { useFSStore } from '@/stores/fsStore'
import { useDebounce } from 'use-debounce'

const UploadProgress = () => {
  const { uploading, uploadProgress } = useFSStore()
  const [debouncedProgress] = useDebounce(uploadProgress, 2)

  if (!uploading) return null

  return (
    <div className="absolute inset-0 z-50 flex flex-col items-center justify-center bg-black/40 backdrop-blur-sm">
      <CircleNotch className="h-12 w-12 animate-spin fill-current text-cyan-400" />
      <p className="mt-4 text-lg font-medium text-cyan-100">Uploading... {Math.round(debouncedProgress)}%</p>
    </div>
  )
}

export default UploadProgress
