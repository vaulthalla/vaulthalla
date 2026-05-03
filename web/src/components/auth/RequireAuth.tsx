'use client'

import { usePathname, useRouter } from 'next/navigation'
import { useEffect, useRef, useState } from 'react'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { useAuthStore } from '@/stores/authStore'

const PUBLIC_ROUTES = new Set<string>(['/login'])
const CHANGE_PASSWORD_ROUTE = '/users/admin/change-password'

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

        const cached = useAuthStore.getState().adminPasswordIsDefault
        const adminPasswordIsDefault = await useAuthStore.getState().fetchAdminPasswordIsDefault(cached === true)

        if (disposed || id !== requestId.current) return

        if (
          (!process.env.NEXT_PUBLIC_VAULTHALLA_DEV_MODE || process.env.NEXT_PUBLIC_VAULTHALLA_DEV_MODE == 'false')
          && adminPasswordIsDefault
          && pathname !== CHANGE_PASSWORD_ROUTE
        ) {
          setChecked(false)
          router.replace(CHANGE_PASSWORD_ROUTE)
          return
        }

        setChecked(true)
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
  return <>{children}</>
}
