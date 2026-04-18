import LoginForm from '@/components/auth/LoginForm'
import { useAuthStore } from '@/stores/authStore'
import { redirect } from 'next/navigation'

export default function LoginPage() {
  if (useAuthStore.getState().token && useAuthStore.getState().user?.name) return redirect('/dashboard')

  return (
    <main className="flex min-h-screen flex-col items-center justify-center">
      <LoginForm />
    </main>
  )
}
