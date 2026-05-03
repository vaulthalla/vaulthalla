'use client'

import React, { useState, useRef } from 'react'
import * as motion from 'motion/react-client'
import { AnimatePresence } from 'motion/react'
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

  const processFiles = React.useCallback((files: FileWithRelativePath[]) => {
    if (disabled) return
    ;(async () => {
      await upload(files)
    })()
      .catch(console.error)
  }, [disabled, upload])

  const dropRef = useFileDrop({
    onFiles: React.useCallback(files => {
      setIsDragging(false)
      dragCounter.current = 0
      processFiles(files)
    }, [processFiles]),
  })

  const handleDragEnter = (e: React.DragEvent) => {
    e.preventDefault()
    if (disabled) return
    dragCounter.current++
    setIsDragging(true)
  }

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault()
    if (disabled) return
    dragCounter.current--
    if (dragCounter.current === 0) {
      setIsDragging(false)
    }
  }

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault()
    if (disabled) e.dataTransfer.dropEffect = 'none'
  }

  return (
    <div
      ref={dropRef}
      onDragEnter={handleDragEnter}
      onDragLeave={handleDragLeave}
      onDragOver={handleDragOver}
      className="relative">
      {children}
      <AnimatePresence>
        {isDragging && (
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 0.5 }}
            exit={{ opacity: 0 }}
            className="absolute inset-0 z-10 border-4 border-dashed border-blue-400 bg-gray-700">
            <div className="flex w-full items-center justify-center text-lg text-white">{disabledMessage}</div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}
