import { File } from '@/models/file'

export async function attachPreview(file: File): Promise<{ src: string; width: number; height: number }> {
  const src = `/preview?vault_id=${file.vault_id}&path=${encodeURIComponent(file.path || file.name)}`
  // Optionally fetch dimensions if your API supports it
  return { src, width: 128, height: 128 } // Or dynamic size
}
