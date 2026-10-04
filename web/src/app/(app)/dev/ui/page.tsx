import { notFound } from 'next/navigation'
import { Gallery } from '@/features/dev/Gallery'

export const metadata = { title: 'UI gallery' }

// Every primitive in every state: the visual regression target for the console. Development builds only.
export default function Page() {
  if (process.env.NODE_ENV === 'production') notFound()
  return <Gallery />
}
