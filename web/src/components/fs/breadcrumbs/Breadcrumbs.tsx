import { cn } from '@/util/cn'
import { Path } from '@/components/fs/breadcrumbs/Path'
import { VaultSelector } from '@/components/fs/breadcrumbs/VaultSelector'

export const Breadcrumbs = ({ className }: { className?: string }) => (
  <nav className={cn('flex items-center gap-2 text-sm text-gray-300', className)}>
    <VaultSelector />
    <Path />
  </nav>
)
