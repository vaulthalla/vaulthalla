import type { PickedFile } from '@/features/files/transfers'

// Collects files from a drop. Every DataTransferItem is read synchronously inside the event (the list is cleared once
// the handler yields), folders are walked afterwards, and items without a filesystem entry fall back to getAsFile.
export const collectDropped = async (dataTransfer: DataTransfer): Promise<PickedFile[]> => {
  const entries: FileSystemEntry[] = []
  const loose: File[] = []
  for (const item of Array.from(dataTransfer.items ?? [])) {
    if (item.kind !== 'file') continue
    const entry = item.webkitGetAsEntry?.()
    if (entry) entries.push(entry)
    else {
      const file = item.getAsFile()
      if (file) loose.push(file)
    }
  }
  if (!entries.length && !loose.length) for (const file of Array.from(dataTransfer.files ?? [])) loose.push(file)

  const picked: PickedFile[] = loose.map(file => ({ file, relativePath: file.name }))
  for (const entry of entries) picked.push(...(await walk(entry, '')))
  return picked
}

const readAll = (reader: FileSystemDirectoryReader) =>
  new Promise<FileSystemEntry[]>((resolve, reject) => {
    const all: FileSystemEntry[] = []
    const next = () =>
      reader.readEntries(batch => {
        if (!batch.length) resolve(all)
        else {
          all.push(...batch)
          next()
        }
      }, reject)
    next()
  })

async function walk(entry: FileSystemEntry, prefix: string): Promise<PickedFile[]> {
  if (entry.isFile) {
    const file = await new Promise<File>((resolve, reject) => (entry as FileSystemFileEntry).file(resolve, reject))
    return [{ file, relativePath: `${prefix}${file.name}` }]
  }
  if (entry.isDirectory) {
    const children = await readAll((entry as FileSystemDirectoryEntry).createReader())
    const nested = await Promise.all(children.map(child => walk(child, `${prefix}${entry.name}/`)))
    return nested.flat()
  }
  return []
}

// <input type="file" webkitdirectory> results carry their folder structure in webkitRelativePath.
export const collectPicked = (files: FileList | null): PickedFile[] =>
  Array.from(files ?? []).map(file => ({ file, relativePath: file.webkitRelativePath || file.name }))
