import { Suspense } from 'react'
import { LoginForm } from '@/features/auth/LoginForm'

export default function LoginPage() {
  return (
    <Suspense>
      <LoginForm />
    </Suspense>
  )
}
