// Visual tones. Status tones (ok/info/warn/danger/unknown) only ever come from a backend severity; accent and
// neutral are decorative.
export type Tone = 'ok' | 'info' | 'warn' | 'danger' | 'unknown' | 'accent' | 'neutral'

export type Severity = 'healthy' | 'info' | 'warning' | 'error' | 'unknown'

// Also covers the component statuses core folds into severities (dashboardOverviewSeverityFromStatus in
// stats/model/DashboardOverview.cpp: "ready", "stale", "stalled", ...), so a raw status gets the overview's tone.
export const severityTone = (severity: string | null | undefined): Tone => {
  switch (severity) {
    case 'healthy':
    case 'ok':
    case 'idle':
    case 'normal':
    case 'ready':
      return 'ok'
    case 'info':
    case 'syncing':
    case 'success':
      return 'info'
    case 'warning':
    case 'warn':
    case 'degraded':
    case 'pressured':
    case 'stale':
      return 'warn'
    case 'error':
    case 'critical':
    case 'failed':
    case 'saturated':
    case 'failing':
    case 'stalled':
    case 'diverged':
    case 'overdue':
      return 'danger'
    default:
      return 'unknown'
  }
}

export const toneClasses: Record<Tone, { text: string; bg: string; border: string; dot: string }> = {
  ok: { text: 'text-ok', bg: 'bg-ok-soft', border: 'border-ok-line', dot: 'bg-ok' },
  info: { text: 'text-info', bg: 'bg-info-soft', border: 'border-info-line', dot: 'bg-info' },
  warn: { text: 'text-warn', bg: 'bg-warn-soft', border: 'border-warn-line', dot: 'bg-warn' },
  danger: { text: 'text-danger', bg: 'bg-danger-soft', border: 'border-danger-line', dot: 'bg-danger' },
  unknown: { text: 'text-unknown', bg: 'bg-unknown-soft', border: 'border-unknown-line', dot: 'bg-unknown' },
  accent: { text: 'text-accent-text', bg: 'bg-accent-soft', border: 'border-accent-line', dot: 'bg-accent' },
  neutral: { text: 'text-fg-muted', bg: 'bg-surface-2', border: 'border-line', dot: 'bg-fg-subtle' },
}

export const severityLabel = (severity: string | null | undefined) => {
  switch (severity) {
    case 'healthy':
      return 'Healthy'
    case 'info':
      return 'Info'
    case 'warning':
      return 'Warning'
    case 'error':
      return 'Error'
    default:
      return 'Unknown'
  }
}
