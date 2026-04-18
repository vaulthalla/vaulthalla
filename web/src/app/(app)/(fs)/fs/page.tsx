import FilesClientPage from '@/app/(app)/(fs)/fs/page.client'
import CopiedItemIndicator from '@/components/fs/CopiedItemIndicator'
import UploadProgress from '@/components/loading/UploadProgress'
import { FileDropOverlay } from '@/components/fs/FileDropOverlay'
import { Breadcrumbs } from '@/components/fs/breadcrumbs/Breadcrumbs'

const FSPage = () => {
  return (
    <>
      <CopiedItemIndicator />
      <Breadcrumbs className="mb-3" />
      <FileDropOverlay>
        <UploadProgress />
        <FilesClientPage />
      </FileDropOverlay>
    </>
  )
}

export default FSPage
