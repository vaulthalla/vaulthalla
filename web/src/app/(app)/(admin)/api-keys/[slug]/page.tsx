import S3APIKeyForm from '@/components/api_keys/S3Form'

const EditApiKeyPage = async ({ params }: { params: Promise<{ slug: string }> }) => {
  const { slug } = await params

  return <S3APIKeyForm edit={true} id={Number(slug)} />
}

export default EditApiKeyPage
