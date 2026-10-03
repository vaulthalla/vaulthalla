'use client'

import { usePathname, useRouter } from 'next/navigation'
import { useEffect, useRef, useState } from 'react'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import InitialPasswordWarning from '@/components/auth/InitialPasswordWarning'
import { useAuthStore } from '@/stores/authStore'

const PUBLIC_ROUTES = new Set<string>(['/login'])

export default function RequireAuth({ children }: { children: React.ReactNode }) {
  const router = useRouter()
  const pathname = usePathname()

  const [checked, setChecked] = useState(false)
  const requestId = useRef(0)

  useEffect(() => {
    if (!pathname) return

    if (PUBLIC_ROUTES.has(pathname)) {
      setChecked(true)
      return
    }

    const id = ++requestId.current
    let disposed = false

    const run = async () => {
      try {
        const store = useAuthStore.getState()

        let authed = await store.isUserAuthenticated()

        if (!authed) {
          try {
            await store.refreshToken()
            authed = await useAuthStore.getState().isUserAuthenticated()
          } catch {
            authed = false
          }
        }

        if (disposed || id !== requestId.current) return

        if (!authed) {
          setChecked(false)
          router.replace('/login')
          return
        }

        setChecked(true)

        // A signed-in session is fully usable. The security posture only feeds a warning, so it is read once per
        // page load in the background and never delays or redirects navigation.
        if (useAuthStore.getState().initialPasswordFile === undefined)
          void useAuthStore.getState().fetchSecurityStatus().catch(() => undefined)
      } catch (err) {
        console.error('RequireAuth failed:', err)
        if (!disposed && id === requestId.current) {
          setChecked(false)
          router.replace('/login')
        }
      }
    }

    void run()

    return () => {
      disposed = true
    }
  }, [pathname, router])

  if (!checked) return <CircleNotchLoader />
  return (
    <>
      <InitialPasswordWarning />
      {children}
    </>
  )
}
