import type {
  DashboardCardSize,
  DashboardLayoutCard,
} from '@/models/dashboard/dashboardLayout'

export function gridClassForSize(size: DashboardCardSize): string {
  if (size === '1x1') return 'md:col-span-3 lg:col-span-3 md:row-span-2'
  if (size === '1x2') return 'md:col-span-3 lg:col-span-3 md:row-span-4'
  if (size === '2x1') return 'md:col-span-6 lg:col-span-6 md:row-span-2'
  if (size === '2x2') return 'md:col-span-6 lg:col-span-6 md:row-span-4'
  if (size === '3x1') return 'md:col-span-6 lg:col-span-9 md:row-span-2'
  if (size === '3x2') return 'md:col-span-6 lg:col-span-9 md:row-span-4'
  return 'md:col-span-6 lg:col-span-12 md:row-span-4'
}

export function baseHeightForCard(layoutCard: DashboardLayoutCard): string {
  if (layoutCard.size === '1x1') return 'min-h-[9.5rem]'
  if (layoutCard.size === '1x2') return 'min-h-[15rem]'
  if (layoutCard.size === '2x1') return 'min-h-[10rem]'
  if (layoutCard.size === '2x2') return 'min-h-[15rem]'
  if (layoutCard.size === '3x1') return 'min-h-[10rem]'
  if (layoutCard.size === '3x2') return 'min-h-[15rem]'
  return 'min-h-[15rem]'
}

const metricCapacityBySize: Record<DashboardCardSize, number> = {
  '1x1': 2,
  '1x2': 8,
  '2x1': 4,
  '2x2': 15,
  '3x1': 5,
  '3x2': 20,
  '4x2': 25,
}

export function metricCountForCard(layoutCard: DashboardLayoutCard): number {
  if (layoutCard.variant === 'graph') {
    if (layoutCard.size === '1x1') return 1
    if (layoutCard.size === '1x2' || layoutCard.size === '2x1') return 2
    if (layoutCard.size === '2x2' || layoutCard.size === '3x1') return 5
    return 8
  }
  const hasVisual = layoutCard.variant === 'visual' || layoutCard.variant === 'hero'
  const capacity = metricCapacityBySize[layoutCard.size]
  if (!hasVisual) return capacity
  if (layoutCard.size === '1x1') return 2
  if (layoutCard.size === '2x1') return 3
  if (layoutCard.size === '1x2') return 6
  return capacity
}

export function metricGridClassForCard(layoutCard: DashboardLayoutCard): string {
  if (layoutCard.variant === 'graph' && (layoutCard.size === '2x1' || layoutCard.size === '1x2')) return 'grid-cols-2'
  if (layoutCard.variant === 'graph' && (layoutCard.size === '2x2' || layoutCard.size === '3x1')) return 'grid-cols-3'
  if (layoutCard.variant === 'graph') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '4x2') return 'grid-cols-2 md:grid-cols-5'
  if (layoutCard.size === '3x2') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '3x1') return 'grid-cols-2 md:grid-cols-5'
  if (layoutCard.size === '2x2') return 'grid-cols-2 md:grid-cols-3'
  if (layoutCard.size === '2x1' && (layoutCard.variant === 'visual' || layoutCard.variant === 'hero')) return 'grid-cols-3'
  if (layoutCard.size === '2x1') return 'grid-cols-2 md:grid-cols-4'
  if (layoutCard.size === '1x2') return 'grid-cols-2'
  return 'grid-cols-2'
}

export function visualHeightClassForCard(layoutCard: DashboardLayoutCard): string {
  if (layoutCard.variant === 'graph' && (layoutCard.size === '1x1' || layoutCard.size === '2x1')) return 'h-14'
  if (layoutCard.variant === 'graph') return 'h-20 flex-1'
  if (layoutCard.size === '3x2' || layoutCard.size === '4x2') return 'h-36'
  if (layoutCard.size === '1x2' || layoutCard.size === '2x2') return 'h-32'
  return ''
}
