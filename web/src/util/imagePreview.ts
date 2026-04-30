import { File } from '@/models/file'
import { buildPreviewUrl } from '@/util/previewUrl'

export async function attachPreview(file: File): Promise<{ src: string; width: number; height: number }> {
  const src = buildPreviewUrl({ mode: 'authenticated', vaultId: file.vault_id, path: file.path || file.name }) || ''
  // Optionally fetch dimensions if your API supports it
  return { src, width: 128, height: 128 } // Or dynamic size
}
