import React from 'react'
import clsx from 'clsx'

interface AdminGridProps {
  children: React.ReactNode
  contentWidth?: 'full' | 'normal' | 'narrow'
  className?: string
}

export const AdminGrid = ({ contentWidth = 'normal', children, className }: AdminGridProps) => {
  const colsClasses =
    contentWidth === 'narrow' ? 'grid-cols-1 lg:grid-cols-2'
    : contentWidth === 'normal' ? 'grid-cols-1 lg:grid-cols-2 2xl:grid-cols-3'
    : 'grid-cols-1 lg:grid-cols-2 xl:grid-cols-2 2xl:grid-cols-3'

  const gapClasses =
    contentWidth === 'narrow' ? 'gap-3 md:gap-4'
    : contentWidth === 'normal' ? 'gap-4 md:gap-5'
    : 'gap-4 md:gap-6'

  return <div className={clsx('grid', colsClasses, gapClasses, className)}>{children}</div>
}
