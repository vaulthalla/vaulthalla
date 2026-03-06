'use client'

import React, { useState, useRef } from 'react'
import * as motion from 'motion/react-client'
import { AnimatePresence } from 'motion/react'
import { useFileDrop } from '@/hooks/useFileDrop'
import { FileWithRelativePath } from '@/models/systemFile'
import { useFSStore } from '@/stores/fsStore'

interface FileDropOverlayProps {
  children: React.ReactNode
}

export const FileDropOverlay: React.FC<FileDropOverlayProps> = ({ children }) => {
  const [isDragging, setIsDragging] = useState(false)
  const dragCounter = useRef(0)

  const { upload, fetchFiles } = useFSStore()

  const processFiles = (files: FileWithRelativePath[]) => {
    ;(async () => {
      upload(files).catch(console.error)
      await fetchFiles()
    })()
  }

  const dropRef = useFileDrop({
    onFiles: files => {
      setIsDragging(false)
      dragCounter.current = 0
      processFiles(files)
    },
  })

  const handleDragEnter = (e: React.DragEvent) => {
    e.preventDefault()
    dragCounter.current++
    setIsDragging(true)
  }

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault()
    dragCounter.current--
    if (dragCounter.current === 0) {
      setIsDragging(false)
    }
  }

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault()
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
            <div className="flex w-full items-center justify-center text-lg text-white">Drop files to upload</div>
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  )
}
