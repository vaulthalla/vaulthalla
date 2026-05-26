import FilesClientPage from '@/app/(app)/(fs)/fs/page.client'
import CopiedItemIndicator from '@/components/fs/CopiedItemIndicator'
import { FileDropOverlay } from '@/components/fs/FileDropOverlay'
import { Breadcrumbs } from '@/components/fs/breadcrumbs/Breadcrumbs'
import { TransferQueueButton } from '@/components/loading/TransferQueueButton'
import { UploadErrorBanner } from '@/components/fs/UploadErrorBanner'

const FSPage = () => {
  return (
    <>
      <CopiedItemIndicator />
      <div className="mb-3 flex items-center justify-between gap-3">
        <Breadcrumbs className="min-w-0 flex-1 overflow-hidden" />
        <TransferQueueButton placement="bottom-left" showSummaryBar sidePanel />
      </div>
      <UploadErrorBanner />
      <FileDropOverlay>
        <FilesClientPage />
      </FileDropOverlay>
    </>
  )
}

export default FSPage
