'use client'

import { create } from 'zustand'
import { persist } from 'zustand/middleware'

// Per-viewer UI conveniences only. Never server data (that lives in the query cache, cleared on logout).
interface UiPrefs {
  railCollapsed: boolean
  fileView: 'list' | 'grid'
  setRailCollapsed: (collapsed: boolean) => void
  setFileView: (view: 'list' | 'grid') => void
}

export const useUiPrefs = create<UiPrefs>()(
  persist(
    set => ({
      railCollapsed: false,
      fileView: 'list',
      setRailCollapsed: railCollapsed => set({ railCollapsed }),
      setFileView: fileView => set({ fileView }),
    }),
    { name: 'vaulthalla-ui', partialize: state => ({ railCollapsed: state.railCollapsed, fileView: state.fileView }) },
  ),
)
