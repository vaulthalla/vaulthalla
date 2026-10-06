import { isPreviewable, type Entry, type PreviewPlan } from '@/features/files/entries'

const NO_PREVIEW: PreviewPlan = { kind: 'unsupported', renderer: 'none', requires: 'preview', thumbnail: false, derived: [] }

// The plan to render: the server's, or the pre-plan behaviour (server JPEG renders of images and PDFs) when an
// older daemon sends none. Lives with the lazy preview code, not in entries.ts (route first-load JS).
export const planOf = (entry: Entry): PreviewPlan => {
  if (entry.kind !== 'file') return NO_PREVIEW
  if (entry.plan) return entry.plan
  if (!isPreviewable(entry)) return NO_PREVIEW
  return { kind: 'rendered_image', renderer: entry.mime === 'application/pdf' ? 'pdf' : 'image', requires: 'preview', thumbnail: true, derived: [] }
}
