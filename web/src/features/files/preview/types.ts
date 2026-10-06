import type { Entry, PreviewPlan } from '@/features/files/entries'
import type { FsSource } from '@/features/files/source'

// Every renderer gets the same props. The sheet has already checked the plan's capability against the source.
export interface RendererProps {
  source: FsSource
  entry: Entry
  plan: PreviewPlan
  onDownload: () => void
  // Renderers holding unsaved work report it; the sheet asks before switching files or closing.
  setDirty: (dirty: boolean) => void
}
