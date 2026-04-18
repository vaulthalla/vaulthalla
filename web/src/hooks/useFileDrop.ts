import { useEffect, useRef } from 'react'
import { FileWithRelativePath } from '@/models/systemFile'

interface UseFileDropOptions {
  onFiles: (files: FileWithRelativePath[]) => void
}

export function useFileDrop({ onFiles }: UseFileDropOptions) {
  const dropRef = useRef<HTMLDivElement | null>(null)

  useEffect(() => {
    const node = dropRef.current
    if (!node) return

    const handleDragOver = (e: DragEvent) => {
      e.preventDefault()
      e.dataTransfer!.dropEffect = 'copy'
    }

    const handleDrop = async (e: DragEvent) => {
      e.preventDefault()
      if (e.dataTransfer?.items) {
        const items = Array.from(e.dataTransfer.items)
        const filePromises: Promise<FileWithRelativePath>[] = []

        for (const item of items) {
          const entry = item.webkitGetAsEntry?.()
          if (entry) {
            filePromises.push(...(await walkEntry(entry)))
          }
        }

        const files = (await Promise.all(filePromises)).flat()
        onFiles(files)
      }
    }

    node.addEventListener('dragover', handleDragOver)
    node.addEventListener('drop', handleDrop)

    return () => {
      node.removeEventListener('dragover', handleDragOver)
      node.removeEventListener('drop', handleDrop)
    }
  }, [onFiles])

  return dropRef
}

// Helper to walk directories recursively
function walkEntry(entry: FileSystemEntry, path = ''): Promise<Promise<FileWithRelativePath>[]> {
  return new Promise((resolve, reject) => {
    if (entry.isFile) {
      ;(entry as FileSystemFileEntry).file(file => {
        // Augment file with relative path
        Object.defineProperty(file, 'relativePath', { value: path + file.name, writable: false })
        resolve([Promise.resolve(file as FileWithRelativePath)])
      }, reject)
    } else if (entry.isDirectory) {
      const dirReader = (entry as FileSystemDirectoryEntry).createReader()
      const promises: Promise<File>[][] = []
      const readEntries = () => {
        dirReader.readEntries(async entries => {
          if (!entries.length) {
            // Flatten nested arrays of promises
            resolve(promises.flat() as Promise<FileWithRelativePath>[])
          } else {
            for (const ent of entries) {
              promises.push(await walkEntry(ent, path + entry.name + '/'))
            }
            readEntries()
          }
        }, reject)
      }
      readEntries()
    }
  })
}
