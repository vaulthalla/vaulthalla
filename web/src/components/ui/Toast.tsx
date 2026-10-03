import { errorMessage } from '@/lib/ws/errors'

// One notification channel for every mutation result. sonner loads on first use (the host mounts at idle).
const sonner = () => import('sonner').then(m => m.toast)

export const notify = {
  success: (message: string, description?: string) => void sonner().then(t => t.success(message, { description })),
  info: (message: string, description?: string) => void sonner().then(t => t(message, { description })),
  error: (error: unknown, fallback = 'Something went wrong') =>
    void sonner().then(t => t.error(errorMessage(error, fallback), { duration: 8000 })),
}
