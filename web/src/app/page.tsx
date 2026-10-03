import { redirect } from 'next/navigation'

// The middleware already sent unauthenticated visitors to /login; everyone else starts in their files.
export default function Home() {
  redirect('/files')
}
