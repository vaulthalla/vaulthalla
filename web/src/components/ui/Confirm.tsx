'use client'

import type React from 'react'
import { create } from 'zustand'

export interface ConfirmRequest {
  title: string
  description?: React.ReactNode
  confirmLabel?: string
  cancelLabel?: string
  tone?: 'danger' | 'primary'
  // When set, the user must type this exact text to enable the confirm button.
  typeToConfirm?: string
}

interface ConfirmState {
  request: (ConfirmRequest & { resolve: (ok: boolean) => void }) | null
}

export const useConfirmStore = create<ConfirmState>(() => ({ request: null }))

// Every destructive action goes through this. Resolves true only on an explicit confirm.
export const confirm = (request: ConfirmRequest): Promise<boolean> =>
  new Promise(resolve => {
    useConfirmStore.getState().request?.resolve(false)
    useConfirmStore.setState({ request: { ...request, resolve } })
  })
