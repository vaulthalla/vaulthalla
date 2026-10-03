'use client'

import { create } from 'zustand'

interface PaletteState {
  open: boolean
  setOpen: (open: boolean) => void
}

export const useCommandPalette = create<PaletteState>(set => ({ open: false, setOpen: open => set({ open }) }))


// ⌘K / Ctrl+K anywhere toggles the palette.
if (typeof window !== 'undefined') {
  window.addEventListener('keydown', event => {
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') {
      event.preventDefault()
      useCommandPalette.setState(state => ({ open: !state.open }))
    }
  })
}
