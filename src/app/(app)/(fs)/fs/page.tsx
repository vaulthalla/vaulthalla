import FilesClientPage from '@/app/(app)/(fs)/fs/page.client'
import VaultBreadcrumbs from '@/components/fs/VaultBreadcrumbs'
import CopiedItemIndicator from '@/components/fs/CopiedItemIndicator'
import UploadProgress from '@/components/loading/UploadProgress'
import { FileDropOverlay } from '@/components/fs/FileDropOverlay'
import React from 'react'

const FSPage = () => {
  return (
    <>
      <CopiedItemIndicator />
      <VaultBreadcrumbs className="mb-3" />
      <FileDropOverlay>
        <UploadProgress />
        <FilesClientPage />
      </FileDropOverlay>
    </>
  )
}

export default FSPage
