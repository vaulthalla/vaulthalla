import { redirect } from 'next/navigation'
import { useAuthStore } from '@/stores/authStore'

const Home = () => {
  const user = useAuthStore.getState().user
  const token = useAuthStore.getState().token

  return token && user?.name ? redirect('/dashboard') : redirect('/login')
}

export default Home
