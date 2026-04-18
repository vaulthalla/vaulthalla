'use client'

import Link from 'next/link'
import clsx from 'clsx'
import { usePathname } from 'next/navigation'

interface ActiveLinkProps {
  href: string
  className?: string
  activeClassName?: string
  partial?: boolean
  children: React.ReactNode
}

export const ActiveLink = ({ href, className, activeClassName, partial = true, children }: ActiveLinkProps) => {
  const pathname = usePathname()

  const isActive = partial ? pathname.startsWith(href) : pathname === href

  return (
    <Link href={href} className={clsx(className, isActive && activeClassName)}>
      {children}
    </Link>
  )
}
