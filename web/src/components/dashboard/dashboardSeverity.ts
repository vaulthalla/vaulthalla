import type { DashboardIssueSummary, DashboardSeverity } from '@/models/stats/dashboardOverview'

export type DashboardSeverityTone = {
  label: string
  border: string
  bg: string
  text: string
  dot: string
  soft: string
  ring: string
  glow: string
}

export const dashboardSeverityTones: Record<DashboardSeverity, DashboardSeverityTone> = {
  healthy: {
    label: 'healthy',
    border: 'border-emerald-300/30',
    bg: 'bg-emerald-400/10',
    text: 'text-emerald-100',
    dot: 'bg-emerald-300',
    soft: 'text-emerald-200/80',
    ring: 'shadow-[0_0_0_1px_rgba(110,231,183,0.12),0_18px_45px_-30px_rgba(52,211,153,0.65)]',
    glow: 'shadow-[0_0_24px_-12px_rgba(52,211,153,0.9)]',
  },
  info: {
    label: 'info',
    border: 'border-cyan-300/30',
    bg: 'bg-cyan-400/10',
    text: 'text-cyan-100',
    dot: 'bg-cyan-300',
    soft: 'text-cyan-200/80',
    ring: 'shadow-[0_0_0_1px_rgba(103,232,249,0.12),0_18px_45px_-30px_rgba(34,211,238,0.65)]',
    glow: 'shadow-[0_0_24px_-12px_rgba(34,211,238,0.9)]',
  },
  warning: {
    label: 'warning',
    border: 'border-amber-300/40',
    bg: 'bg-amber-400/12',
    text: 'text-amber-100',
    dot: 'bg-amber-300',
    soft: 'text-amber-200/80',
    ring: 'shadow-[0_0_0_1px_rgba(252,211,77,0.16),0_18px_45px_-28px_rgba(251,191,36,0.8)]',
    glow: 'shadow-[0_0_26px_-10px_rgba(251,191,36,0.9)]',
  },
  error: {
    label: 'error',
    border: 'border-rose-300/45',
    bg: 'bg-rose-400/12',
    text: 'text-rose-100',
    dot: 'bg-rose-300',
    soft: 'text-rose-200/80',
    ring: 'shadow-[0_0_0_1px_rgba(253,164,175,0.18),0_18px_45px_-28px_rgba(244,63,94,0.85)]',
    glow: 'shadow-[0_0_28px_-10px_rgba(244,63,94,0.95)]',
  },
  unknown: {
    label: 'unknown',
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/70',
    dot: 'bg-white/35',
    soft: 'text-white/55',
    ring: 'shadow-[0_18px_45px_-35px_rgba(255,255,255,0.2)]',
    glow: 'shadow-[0_0_22px_-14px_rgba(255,255,255,0.3)]',
  },
  unavailable: {
    label: 'unavailable',
    border: 'border-white/10',
    bg: 'bg-white/5',
    text: 'text-white/55',
    dot: 'bg-white/25',
    soft: 'text-white/45',
    ring: 'shadow-[0_18px_45px_-35px_rgba(255,255,255,0.18)]',
    glow: 'shadow-[0_0_20px_-14px_rgba(255,255,255,0.25)]',
  },
}

export function dashboardSeverityTone(severity: DashboardSeverity): DashboardSeverityTone {
  return dashboardSeverityTones[severity] ?? dashboardSeverityTones.unknown
}

export function dashboardSeverityRank(severity: DashboardSeverity): number {
  if (severity === 'error') return 5
  if (severity === 'warning') return 4
  if (severity === 'unknown') return 3
  if (severity === 'info') return 2
  if (severity === 'unavailable') return 1
  return 0
}

export function worstDashboardSeverity(...severities: DashboardSeverity[]): DashboardSeverity {
  return severities.reduce(
    (worst, severity) => (dashboardSeverityRank(severity) > dashboardSeverityRank(worst) ? severity : worst),
    'healthy' as DashboardSeverity,
  )
}

export function dashboardIssueCountLabel(errorCount: number, warningCount: number): string | null {
  if (errorCount > 0 && warningCount > 0) return `${errorCount} error${errorCount === 1 ? '' : 's'} · ${warningCount} warning${warningCount === 1 ? '' : 's'}`
  if (errorCount > 0) return `${errorCount} error${errorCount === 1 ? '' : 's'}`
  if (warningCount > 0) return `${warningCount} warning${warningCount === 1 ? '' : 's'}`
  return null
}

export function dashboardIssueCount(errorCount: number, warningCount: number): number {
  return errorCount > 0 ? errorCount : warningCount
}

export function sortDashboardIssues<T extends DashboardIssueSummary>(issues: T[]): T[] {
  return [...issues].sort((a, b) => dashboardSeverityRank(b.severity) - dashboardSeverityRank(a.severity))
}
