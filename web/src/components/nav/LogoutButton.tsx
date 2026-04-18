'use client'

import { useAuthStore } from '@/stores/authStore'
import { Button } from '@/components/Button'
import { useRouter } from 'next/navigation'

export const LogoutButton = ({ isCompact }: { isCompact: boolean }) => {
  const router = useRouter()

  return (
    <Button
      variant="destructive"
      onClick={() => {
        useAuthStore.getState().logout()
        router.push('/login')
      }}>
      {isCompact ? '⏻' : 'Logout'}
    </Button>
  )
}
