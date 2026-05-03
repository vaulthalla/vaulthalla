import SharePageClient from '@/app/share/[token]/page.client'

const SharePage = async ({ params }: { params: Promise<{ token: string }> }) => {
  const { token } = await params
  return <SharePageClient token={decodeURIComponent(token)} />
}

export default SharePage
