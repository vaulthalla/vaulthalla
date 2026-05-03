import FilesClientPage from '@/app/(app)/(fs)/fs/page.client'
import CopiedItemIndicator from '@/components/fs/CopiedItemIndicator'
import UploadProgress from '@/components/loading/UploadProgress'
import { FileDropOverlay } from '@/components/fs/FileDropOverlay'
import { Breadcrumbs } from '@/components/fs/breadcrumbs/Breadcrumbs'
import { DownloadCurrentDirectoryButton } from '@/components/fs/DownloadCurrentDirectoryButton'

const FSPage = () => {
  return (
    <>
      <CopiedItemIndicator />
      <div className="mb-3 flex items-center justify-between gap-3">
        <Breadcrumbs className="min-w-0 flex-1" />
        <DownloadCurrentDirectoryButton />
      </div>
      <FileDropOverlay>
        <UploadProgress />
        <FilesClientPage />
      </FileDropOverlay>
    </>
  )
}

export default FSPage
