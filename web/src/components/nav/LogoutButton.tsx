'use client'

import { useAuthStore } from '@/stores/authStore'
import { Button } from '@/components/Button'
import { useRouter } from 'next/navigation'

export const LogoutButton = ({ isCompact }: { isCompact: boolean }) => {
  const router = useRouter()

  return (
    <Button
      variant="destructive"
      size={isCompact ? 'icon' : 'default'}
      aria-label={isCompact ? 'Logout' : undefined}
      title={isCompact ? 'Logout' : undefined}
      className={isCompact ? 'my-0 h-10 w-10 p-0 text-lg' : undefined}
      onClick={() => {
        useAuthStore.getState().logout()
        router.push('/login')
      }}>
      {isCompact ? '⏻' : 'Logout'}
    </Button>
  )
}
