import type {
  DashboardCardSize,
} from '@/models/dashboard/dashboardLayout'

export interface DashboardCardSizeTemplate {
  size: DashboardCardSize
  colSpanMd: number
  colSpanLg: number
  rowSpan: number
  cardHeightClass: string
  headerPx: number
  summaryLines: 1 | 2
  tileColsMd: number
  tileColsLg: number
  tileRows: number
  tileHeightPx: number
  tileGapPx: number
  visualHeightClass: string
  maxTiles: number
  requiresVisual: boolean
  cardGridClass: string
  metricGridClass: string
}

export const dashboardCardSizeTemplates: Record<DashboardCardSize, DashboardCardSizeTemplate> = {
  '1x1': {
    size: '1x1',
    colSpanMd: 3,
    colSpanLg: 3,
    rowSpan: 2,
    cardHeightClass: 'min-h-[9.5rem]',
    headerPx: 58,
    summaryLines: 1,
    tileColsMd: 2,
    tileColsLg: 2,
    tileRows: 1,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: '',
    maxTiles: 2,
    requiresVisual: false,
    cardGridClass: 'md:col-span-3 lg:col-span-3 md:row-span-2',
    metricGridClass: 'grid-cols-2',
  },
  '1x2': {
    size: '1x2',
    colSpanMd: 3,
    colSpanLg: 3,
    rowSpan: 4,
    cardHeightClass: 'min-h-[15rem]',
    headerPx: 64,
    summaryLines: 1,
    tileColsMd: 2,
    tileColsLg: 2,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-32',
    maxTiles: 4,
    requiresVisual: true,
    cardGridClass: 'md:col-span-3 lg:col-span-3 md:row-span-4',
    metricGridClass: 'grid-cols-2',
  },
  '2x1': {
    size: '2x1',
    colSpanMd: 6,
    colSpanLg: 6,
    rowSpan: 2,
    cardHeightClass: 'min-h-[10rem]',
    headerPx: 58,
    summaryLines: 1,
    tileColsMd: 4,
    tileColsLg: 4,
    tileRows: 1,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-14',
    maxTiles: 4,
    requiresVisual: false,
    cardGridClass: 'md:col-span-6 lg:col-span-6 md:row-span-2',
    metricGridClass: 'grid-cols-2 md:grid-cols-4',
  },
  '2x2': {
    size: '2x2',
    colSpanMd: 6,
    colSpanLg: 6,
    rowSpan: 4,
    cardHeightClass: 'min-h-[15rem]',
    headerPx: 64,
    summaryLines: 2,
    tileColsMd: 3,
    tileColsLg: 3,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-32',
    maxTiles: 6,
    requiresVisual: true,
    cardGridClass: 'md:col-span-6 lg:col-span-6 md:row-span-4',
    metricGridClass: 'grid-cols-2 md:grid-cols-3',
  },
  '3x1': {
    size: '3x1',
    colSpanMd: 6,
    colSpanLg: 9,
    rowSpan: 2,
    cardHeightClass: 'min-h-[10rem]',
    headerPx: 58,
    summaryLines: 1,
    tileColsMd: 4,
    tileColsLg: 5,
    tileRows: 1,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-14',
    maxTiles: 5,
    requiresVisual: false,
    cardGridClass: 'md:col-span-6 lg:col-span-9 md:row-span-2',
    metricGridClass: 'grid-cols-2 md:grid-cols-5',
  },
  '3x2': {
    size: '3x2',
    colSpanMd: 6,
    colSpanLg: 9,
    rowSpan: 4,
    cardHeightClass: 'min-h-[15rem]',
    headerPx: 64,
    summaryLines: 2,
    tileColsMd: 4,
    tileColsLg: 4,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-36',
    maxTiles: 8,
    requiresVisual: true,
    cardGridClass: 'md:col-span-6 lg:col-span-9 md:row-span-4',
    metricGridClass: 'grid-cols-2 md:grid-cols-4',
  },
  '4x2': {
    size: '4x2',
    colSpanMd: 6,
    colSpanLg: 12,
    rowSpan: 4,
    cardHeightClass: 'min-h-[15rem]',
    headerPx: 64,
    summaryLines: 2,
    tileColsMd: 5,
    tileColsLg: 5,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-36',
    maxTiles: 10,
    requiresVisual: true,
    cardGridClass: 'md:col-span-6 lg:col-span-12 md:row-span-4',
    metricGridClass: 'grid-cols-2 md:grid-cols-5',
  },
}

export function templateForCardSize(size: DashboardCardSize): DashboardCardSizeTemplate {
  return dashboardCardSizeTemplates[size]
}
