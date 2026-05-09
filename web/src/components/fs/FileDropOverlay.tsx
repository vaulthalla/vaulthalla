'use client'

import React, { useState, useRef } from 'react'
import { useFileDrop } from '@/hooks/useFileDrop'
import { FileWithRelativePath } from '@/models/systemFile'
import { useFSStore } from '@/stores/fsStore'

interface FileDropOverlayProps {
  children: React.ReactNode
  disabled?: boolean
  disabledMessage?: string
}

export const FileDropOverlay: React.FC<FileDropOverlayProps> = ({ children, disabled = false, disabledMessage = 'Upload is not available' }) => {
  const [isDragging, setIsDragging] = useState(false)
  const dragCounter = useRef(0)

  const upload = useFSStore(state => state.upload)
  const overlayMessage = disabled ? disabledMessage : 'Drop files to upload'

  const isFileDrag = (e: React.DragEvent) => Array.from(e.dataTransfer.types).includes('Files')

  const setDragging = React.useCallback((next: boolean) => {
    setIsDragging(current => current === next ? current : next)
  }, [])

  const resetDrag = React.useCallback(() => {
    dragCounter.current = 0
    setDragging(false)
  }, [setDragging])

  const processFiles = React.useCallback((files: FileWithRelativePath[]) => {
    if (disabled) return
    ;(async () => {
      await upload(files)
    })()
      .catch(console.error)
  }, [disabled, upload])

  const dropRef = useFileDrop({
    onFiles: React.useCallback(files => {
      resetDrag()
      processFiles(files)
    }, [processFiles, resetDrag]),
  })

  const handleDragEnter = (e: React.DragEvent) => {
    if (!isFileDrag(e)) return
    e.preventDefault()
    dragCounter.current++
    setDragging(true)
  }

  const handleDragLeave = (e: React.DragEvent) => {
    if (!isFileDrag(e)) return
    e.preventDefault()
    dragCounter.current = Math.max(0, dragCounter.current - 1)
    if (dragCounter.current === 0) {
      setDragging(false)
    }
  }

  const handleDragOver = (e: React.DragEvent) => {
    if (!isFileDrag(e)) return
    e.preventDefault()
    e.dataTransfer.dropEffect = disabled ? 'none' : 'copy'
  }

  const handleDrop = (e: React.DragEvent) => {
    if (isFileDrag(e)) e.preventDefault()
    resetDrag()
  }

  return (
    <div
      ref={dropRef}
      onDragEnter={handleDragEnter}
      onDragLeave={handleDragLeave}
      onDragOver={handleDragOver}
      onDragEnd={resetDrag}
      onDrop={handleDrop}
      className="relative">
      {children}
      <div
        aria-hidden={!isDragging}
        className={`pointer-events-none absolute inset-0 z-10 flex items-center justify-center border-4 border-dashed border-blue-400 bg-gray-700/50 text-lg text-white transition-opacity duration-100 ${
          isDragging ? 'opacity-100' : 'opacity-0'
        }`}>
        <div className="px-4 text-center">{overlayMessage}</div>
      </div>
    </div>
  )
}
