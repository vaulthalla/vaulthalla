import type {
  DashboardCardSize,
} from '@/models/dashboard/dashboardLayout'

export interface DashboardCardSizeTemplate {
  size: DashboardCardSize
  colSpanMd: number
  colSpanLg: number
  rowSpan: number
  cardHeightRem: number
  cardHeightClass: string
  headerPx: number
  summaryLines: 1 | 2
  tileColsMd: number
  tileColsLg: number
  tileRows: number
  tileHeightPx: number
  tileGapPx: number
  visualHeightClass: string
  tileSlotCount: number
  requiresVisual: boolean
  cardGridClass: string
  metricGridClass: string
}

export const DASHBOARD_GRID_ROW_HEIGHT_REM = 5
export const DASHBOARD_GRID_ROW_GAP_REM = 0.75
export const DASHBOARD_GRID_ROW_HEIGHT_CLASS = 'md:auto-rows-[5rem]'
export const DASHBOARD_GRID_ROW_GAP_CLASS = 'gap-3'
export const DASHBOARD_CARD_ROW_SPAN_HEIGHT_PROPERTY = '--dashboard-card-row-span-height'

export function dashboardGridRowSpanHeightRem(rowSpan: number): number {
  return rowSpan * DASHBOARD_GRID_ROW_HEIGHT_REM + Math.max(0, rowSpan - 1) * DASHBOARD_GRID_ROW_GAP_REM
}

function cardHeightClassForRowSpan(rowSpan: 2 | 4): string {
  if (rowSpan === 2) {
    return 'min-h-[10rem] md:h-[var(--dashboard-card-row-span-height)]'
  }

  return 'min-h-[15rem] md:h-[var(--dashboard-card-row-span-height)]'
}

export const dashboardCardSizeTemplates: Record<DashboardCardSize, DashboardCardSizeTemplate> = {
  '1x1': {
    size: '1x1',
    colSpanMd: 3,
    colSpanLg: 3,
    rowSpan: 2,
    cardHeightRem: dashboardGridRowSpanHeightRem(2),
    cardHeightClass: cardHeightClassForRowSpan(2),
    headerPx: 58,
    summaryLines: 1,
    tileColsMd: 2,
    tileColsLg: 2,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: '',
    tileSlotCount: 4,
    requiresVisual: false,
    cardGridClass: 'md:col-span-3 lg:col-span-3 md:row-span-2',
    metricGridClass: 'grid-cols-2',
  },
  '1x2': {
    size: '1x2',
    colSpanMd: 3,
    colSpanLg: 3,
    rowSpan: 4,
    cardHeightRem: dashboardGridRowSpanHeightRem(4),
    cardHeightClass: cardHeightClassForRowSpan(4),
    headerPx: 64,
    summaryLines: 1,
    tileColsMd: 2,
    tileColsLg: 2,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-32',
    tileSlotCount: 4,
    requiresVisual: true,
    cardGridClass: 'md:col-span-3 lg:col-span-3 md:row-span-4',
    metricGridClass: 'grid-cols-2',
  },
  '2x1': {
    size: '2x1',
    colSpanMd: 6,
    colSpanLg: 6,
    rowSpan: 2,
    cardHeightRem: dashboardGridRowSpanHeightRem(2),
    cardHeightClass: cardHeightClassForRowSpan(2),
    headerPx: 58,
    summaryLines: 1,
    tileColsMd: 4,
    tileColsLg: 4,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-14',
    tileSlotCount: 8,
    requiresVisual: false,
    cardGridClass: 'md:col-span-6 lg:col-span-6 md:row-span-2',
    metricGridClass: 'grid-cols-2 md:grid-cols-4',
  },
  '2x2': {
    size: '2x2',
    colSpanMd: 6,
    colSpanLg: 6,
    rowSpan: 4,
    cardHeightRem: dashboardGridRowSpanHeightRem(4),
    cardHeightClass: cardHeightClassForRowSpan(4),
    headerPx: 64,
    summaryLines: 2,
    tileColsMd: 4,
    tileColsLg: 4,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-32',
    tileSlotCount: 8,
    requiresVisual: true,
    cardGridClass: 'md:col-span-6 lg:col-span-6 md:row-span-4',
    metricGridClass: 'grid-cols-2 md:grid-cols-4',
  },
  '3x1': {
    size: '3x1',
    colSpanMd: 6,
    colSpanLg: 9,
    rowSpan: 2,
    cardHeightRem: dashboardGridRowSpanHeightRem(2),
    cardHeightClass: cardHeightClassForRowSpan(2),
    headerPx: 58,
    summaryLines: 1,
    tileColsMd: 4,
    tileColsLg: 5,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-14',
    tileSlotCount: 10,
    requiresVisual: false,
    cardGridClass: 'md:col-span-6 lg:col-span-9 md:row-span-2',
    metricGridClass: 'grid-cols-2 md:grid-cols-5',
  },
  '3x2': {
    size: '3x2',
    colSpanMd: 6,
    colSpanLg: 9,
    rowSpan: 4,
    cardHeightRem: dashboardGridRowSpanHeightRem(4),
    cardHeightClass: cardHeightClassForRowSpan(4),
    headerPx: 64,
    summaryLines: 2,
    tileColsMd: 4,
    tileColsLg: 4,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-36',
    tileSlotCount: 8,
    requiresVisual: true,
    cardGridClass: 'md:col-span-6 lg:col-span-9 md:row-span-4',
    metricGridClass: 'grid-cols-2 md:grid-cols-4',
  },
  '4x2': {
    size: '4x2',
    colSpanMd: 6,
    colSpanLg: 12,
    rowSpan: 4,
    cardHeightRem: dashboardGridRowSpanHeightRem(4),
    cardHeightClass: cardHeightClassForRowSpan(4),
    headerPx: 64,
    summaryLines: 2,
    tileColsMd: 5,
    tileColsLg: 5,
    tileRows: 2,
    tileHeightPx: 44,
    tileGapPx: 6,
    visualHeightClass: 'h-36',
    tileSlotCount: 10,
    requiresVisual: true,
    cardGridClass: 'md:col-span-6 lg:col-span-12 md:row-span-4',
    metricGridClass: 'grid-cols-2 md:grid-cols-5',
  },
}

export function templateForCardSize(size: DashboardCardSize): DashboardCardSizeTemplate {
  return dashboardCardSizeTemplates[size]
}
