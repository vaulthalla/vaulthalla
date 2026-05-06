'use client'

import Logo from '@/components/Logo'
import { useAuthStore } from '@/stores/authStore'
import { usePathname, useRouter } from 'next/navigation'
import { Button } from '@/components/Button'
import GaugeHigh from '@/fa-duotone/gauge-high.svg'
import FolderOpen from '@/fa-duotone/folder-open.svg'

export const Banner = () => {
  const user = useAuthStore.getState().user

  return (
    <div className="flex flex-col items-center space-y-2">
      <Logo />
      {user?.name && <h3 className="text-center text-2xl">Welcome, {user.name}!</h3>}
    </div>
  )
}

export const ToggleNavButton = ({ isCompact }: { isCompact: boolean }) => {
  const router = useRouter()

  const isFilesPage = usePathname().startsWith('/fs')
  const Icon = isFilesPage ? GaugeHigh : FolderOpen
  const route = isFilesPage ? '/dashboard' : '/fs'
  const buttonText = isFilesPage ? 'Dashboard' : 'Filesystem'

  if (isCompact)
    return (
      <button
        type="button"
        aria-label={buttonText}
        title={buttonText}
        className="flex h-10 w-full items-center justify-center rounded-xl border border-cyan-300/15 bg-cyan-400/8 text-primary transition hover:border-cyan-200/35 hover:bg-cyan-400/15"
        onClick={() => router.push(route)}>
        <Icon className="h-6 w-6 fill-current" aria-hidden="true" />
      </button>
    )

  return (
    <Button
      variant="glow"
      type="button"
      onClick={() => router.push(route)}
      className="mb-4 flex items-center gap-4 text-xl font-thin">
      {buttonText} <Icon className="cursor-pointer fill-current" />
    </Button>
  )
}

export const NavFooter = () => <div className="text-xs text-cyan-400 opacity-70">v0.1.0 • Vaulthalla</div>

export const NavSpacer = () => <div className="grow" />
