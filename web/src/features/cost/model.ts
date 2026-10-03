// S3 price budgets (pricing.* and s3.gateway.budget.*). Normalizers keep unknown values unknown: a missing number is
// null, never 0, so the UI can say "not available" instead of inventing a healthy-looking zero.

export type BudgetScope = 'global' | 'provider' | 'vault' | 'gateway_credential' | 'gateway_credential_vault'
export type BudgetMode = 'off' | 'report' | 'warn' | 'enforce'

export const MODES: { value: BudgetMode; label: string; hint: string }[] = [
  { value: 'off', label: 'Off', hint: 'No checks.' },
  { value: 'report', label: 'Report', hint: 'Record estimated spend; never warn or block.' },
  { value: 'warn', label: 'Warn', hint: 'Notify when a limit would be exceeded, but let syncs run.' },
  { value: 'enforce', label: 'Enforce', hint: 'Stall syncs that would exceed a limit until an override is approved.' },
]

// Provider keys core accepts for provider-scope budgets (ops::pricing: aws-s3, cloudflare-r2).
export const BUDGET_PROVIDERS = [
  { key: 'aws-s3', label: 'AWS S3' },
  { key: 'cloudflare-r2', label: 'Cloudflare R2' },
]

export const DEFAULT_CATALOG_AGE_SECONDS = 43_200

type Rec = Record<string, unknown>
export const rec = (v: unknown): Rec => (v && typeof v === 'object' && !Array.isArray(v) ? (v as Rec) : {})
export const str = (v: unknown): string | null =>
  typeof v === 'string' && v.trim() !== '' ? v
  : typeof v === 'number' && Number.isFinite(v) ? String(v)
  : null
export const num = (v: unknown): number | null => {
  if (typeof v === 'number') return Number.isFinite(v) ? v : null
  if (typeof v === 'string' && v.trim() !== '') {
    const n = Number(v)
    return Number.isFinite(n) ? n : null
  }
  return null
}
export const bool = (v: unknown): boolean | null => (typeof v === 'boolean' ? v : null)
const list = <T>(v: unknown, map: (x: unknown) => T): T[] => (Array.isArray(v) ? v.map(map) : [])
const strings = (v: unknown): string[] => (Array.isArray(v) ? v.map(str).filter((x): x is string => x !== null) : [])

const asScope = (v: unknown): BudgetScope =>
  v === 'provider' || v === 'vault' || v === 'gateway_credential' || v === 'gateway_credential_vault' ? v : 'global'
const asMode = (v: unknown): BudgetMode =>
  v === 'off' || v === 'report' || v === 'warn' || v === 'enforce' ? v : 'off'
const asWindow = (v: unknown): string | null => {
  if (v === 'monthly' || v === 'month') return 'monthly'
  if (v === 'daily' || v === 'day') return 'daily'
  if (v === 'per_run' || v === 'run') return 'per_run'
  return str(v)
}

export interface BudgetPolicy {
  id: number | null
  scope: BudgetScope
  provider_key: string | null
  vault_id: number | null
  gateway_credential_id: number | null
  mode: BudgetMode
  currency: string
  max_run_cost: string | null
  max_daily_cost: string | null
  max_monthly_cost: string | null
  require_verified_catalog: boolean
  allow_stale_catalog: boolean
  max_catalog_age_seconds: number | null
  is_active: boolean
}

export const toPolicy = (input: unknown): BudgetPolicy => {
  const d = rec(input)
  return {
    id: num(d.id),
    scope: asScope(d.scope),
    provider_key: str(d.provider_key),
    vault_id: num(d.vault_id),
    gateway_credential_id: num(d.gateway_credential_id),
    mode: asMode(d.mode),
    currency: str(d.currency) ?? 'USD',
    max_run_cost: str(d.max_run_cost),
    max_daily_cost: str(d.max_daily_cost),
    max_monthly_cost: str(d.max_monthly_cost),
    require_verified_catalog: bool(d.require_verified_catalog) ?? true,
    allow_stale_catalog: bool(d.allow_stale_catalog) ?? false,
    max_catalog_age_seconds: num(d.max_catalog_age_seconds),
    is_active: bool(d.is_active) ?? true,
  }
}

export interface PolicyPayload {
  scope: BudgetScope
  provider_key: string | null
  vault_id: number | null
  gateway_credential_id: number | null
  mode: BudgetMode
  currency: string
  max_run_cost: string | null
  max_daily_cost: string | null
  max_monthly_cost: string | null
  require_verified_catalog: boolean
  allow_stale_catalog: boolean
  max_catalog_age_seconds: number | null
}

export interface BudgetTrend {
  scope: BudgetScope
  provider_key: string | null
  vault_id: number | null
  gateway_credential_id: number | null
  policy_id: number | null
  currency: string
  window_type: string | null
  window_start: string | null
  window_end: string | null
  committed_cost: string | null
  reserved_cost: string | null
  total_cost: string | null
  limit: string | null
  remaining: string | null
  percent_used: number | null
  projected_window_cost: string | null
  projected_overage: string | null
  predicted_exhaustion_at: string | null
  confidence: string | null
  recent_daily_average: string | null
  recent_7d_average: string | null
  recent_30d_average: string | null
  warnings: string[]
}

export const toTrend = (input: unknown): BudgetTrend => {
  const d = rec(input)
  return {
    scope: asScope(d.scope),
    provider_key: str(d.provider_key),
    vault_id: num(d.vault_id),
    gateway_credential_id: num(d.gateway_credential_id),
    policy_id: num(d.policy_id),
    currency: str(d.currency) ?? 'USD',
    window_type: asWindow(d.window_type),
    window_start: str(d.window_start),
    window_end: str(d.window_end),
    committed_cost: str(d.committed_cost),
    reserved_cost: str(d.reserved_cost),
    total_cost: str(d.total_cost),
    limit: str(d.limit),
    remaining: str(d.remaining),
    percent_used: num(d.percent_used),
    projected_window_cost: str(d.projected_window_cost),
    projected_overage: str(d.projected_overage),
    predicted_exhaustion_at: str(d.predicted_exhaustion_at),
    confidence: str(d.confidence),
    recent_daily_average: str(d.recent_daily_average),
    recent_7d_average: str(d.recent_7d_average),
    recent_30d_average: str(d.recent_30d_average),
    warnings: strings(d.warnings),
  }
}

export interface LedgerEntry {
  id: number | null
  policy_id: number | null
  run_uuid: string | null
  gateway_credential_id: number | null
  request_uuid: string | null
  operation: string | null
  object_key: string | null
  vault_id: number | null
  provider_key: string | null
  currency: string
  window: string | null
  reserved_cost: string | null
  committed_cost: string | null
  estimated_cost: string | null
  usage_source: string | null
  synthetic: boolean
  status: string | null
  created_at: string | null
}

export const toLedger = (input: unknown): LedgerEntry => {
  const d = rec(input)
  return {
    id: num(d.id),
    policy_id: num(d.policy_id),
    run_uuid: str(d.run_uuid),
    gateway_credential_id: num(d.gateway_credential_id),
    request_uuid: str(d.request_uuid),
    operation: str(d.operation),
    object_key: str(d.object_key),
    vault_id: num(d.vault_id),
    provider_key: str(d.provider_key),
    currency: str(d.currency) ?? 'USD',
    window: asWindow(d.window),
    reserved_cost: str(d.reserved_cost),
    committed_cost: str(d.committed_cost),
    estimated_cost: str(d.estimated_cost),
    usage_source: str(d.usage_source),
    synthetic: bool(d.synthetic) ?? false,
    status: str(d.status),
    created_at: str(d.created_at),
  }
}

export interface PriceNotification {
  id: number
  type: string | null
  severity: string | null
  title: string | null
  message: string | null
  scope: string | null
  vault_id: number | null
  provider_key: string | null
  policy_id: number | null
  run_uuid: string | null
  acknowledged_at: string | null
  created_at: string | null
}

export const toNotification = (input: unknown): PriceNotification => {
  const d = rec(input)
  return {
    id: num(d.id) ?? 0,
    type: str(d.type),
    severity: str(d.severity),
    title: str(d.title),
    message: str(d.message),
    scope: str(d.scope),
    vault_id: num(d.vault_id),
    provider_key: str(d.provider_key),
    policy_id: num(d.policy_id),
    run_uuid: str(d.run_uuid),
    acknowledged_at: str(d.acknowledged_at),
    created_at: str(d.created_at),
  }
}

export interface PriceOverride {
  id: number
  run_uuid: string | null
  vault_id: number | null
  requested_by: number | null
  approved_by: number | null
  status: string
  reason: string | null
  policy_ids: number[]
  estimated_cost: string | null
  currency: string
  expires_at: string | null
  created_at: string | null
  decided_at: string | null
  used_at: string | null
}

export const toOverride = (input: unknown): PriceOverride => {
  const d = rec(input)
  return {
    id: num(d.id) ?? 0,
    run_uuid: str(d.run_uuid),
    vault_id: num(d.vault_id),
    requested_by: num(d.requested_by),
    approved_by: num(d.approved_by),
    status: str(d.status) ?? 'requested',
    reason: str(d.reason),
    policy_ids: list(d.policy_ids, num).filter((x): x is number => x !== null),
    estimated_cost: str(d.estimated_cost),
    currency: str(d.currency) ?? 'USD',
    expires_at: str(d.expires_at),
    created_at: str(d.created_at),
    decided_at: str(d.decided_at),
    used_at: str(d.used_at),
  }
}

export interface BudgetStatus {
  policies: BudgetPolicy[]
  ledger: LedgerEntry[]
  trends: BudgetTrend[]
  notifications: PriceNotification[]
  overrides: PriceOverride[]
}

export const toStatus = (input: unknown): BudgetStatus => {
  const d = rec(input)
  return {
    policies: list(d.policies, toPolicy),
    ledger: list(d.ledger, toLedger),
    trends: list(d.trends, toTrend),
    notifications: list(d.notifications, toNotification),
    overrides: list(d.overrides, toOverride),
  }
}

export interface BudgetStats {
  active_policies: number | null
  blocked_syncs_24h: number | null
  warning_notifications: number | null
  critical_notifications: number | null
  unacknowledged_notifications: number | null
  pending_overrides: number | null
  // Sums over monthly budget windows only. Null when no monthly window exists (spend outside budgets isn't tracked).
  current_monthly_spend: string | null
  projected_monthly_spend: string | null
  currency: string
  trends: BudgetTrend[]
}

export const toStats = (input: unknown): BudgetStats => {
  const d = rec(input)
  const trends = list(d.trends, toTrend)
  const monthly = trends.filter(t => t.window_type === 'monthly')
  return {
    active_policies: num(d.active_policies),
    blocked_syncs_24h: num(d.blocked_syncs_24h),
    warning_notifications: num(d.warning_notifications),
    critical_notifications: num(d.critical_notifications),
    unacknowledged_notifications: num(d.unacknowledged_notifications),
    pending_overrides: num(d.pending_overrides),
    // Core starts both sums at "0" and only adds monthly windows; with none there's nothing measured.
    current_monthly_spend: monthly.length ? str(d.current_monthly_spend) : null,
    projected_monthly_spend:
      monthly.some(t => t.projected_window_cost !== null) ? str(d.projected_monthly_spend) : null,
    currency: str(d.currency) ?? 'USD',
    trends,
  }
}

export interface WindowCheck {
  policy_id: number | null
  scope: string | null
  mode: string | null
  window: string | null
  currency: string
  limit: string | null
  used_before: string | null
  remaining_before: string | null
  requested: string | null
  exceeded: boolean
}

const toCheck = (input: unknown): WindowCheck => {
  const d = rec(input)
  return {
    policy_id: num(d.policy_id),
    scope: str(d.scope),
    mode: str(d.mode),
    window: asWindow(d.window),
    currency: str(d.currency) ?? 'USD',
    limit: str(d.limit),
    used_before: str(d.used_before),
    remaining_before: str(d.remaining_before),
    requested: str(d.requested),
    exceeded: bool(d.exceeded) ?? false,
  }
}

export interface PreflightResult {
  decision: {
    allowed: boolean | null
    stalled: boolean
    warnings: string[]
    reason: string | null
    currency: string
    limit: string | null
    remaining_before: string | null
    requested: string | null
    checks: WindowCheck[]
  }
  estimate: {
    available: boolean | null
    supported: boolean | null
    stale: boolean | null
    estimated_cost: string | null
    currency: string | null
    catalog_verified: boolean | null
    catalog_version: string | null
    catalog_age_seconds: number | null
    confidence_level: string | null
    unknowns: string[]
    unavailable_reason: string | null
  }
  plan: Record<string, number | null>
}

export const toPreflight = (input: unknown): PreflightResult => {
  const d = rec(input)
  const decision = rec(d.decision)
  const estimate = rec(d.estimate)
  return {
    decision: {
      allowed: bool(decision.allowed),
      stalled: bool(decision.stalled) ?? false,
      warnings: strings(decision.warnings),
      reason: str(decision.reason),
      currency: str(decision.currency) ?? 'USD',
      limit: str(decision.limit),
      remaining_before: str(decision.remaining_before),
      requested: str(decision.requested),
      checks: list(decision.checks, toCheck),
    },
    estimate: {
      available: bool(estimate.available),
      supported: bool(estimate.supported),
      stale: bool(estimate.stale),
      estimated_cost: str(estimate.estimated_cost),
      currency: str(estimate.currency),
      catalog_verified: bool(estimate.catalog_verified),
      catalog_version: str(estimate.catalog_version),
      catalog_age_seconds: num(estimate.catalog_age_seconds),
      confidence_level: str(estimate.confidence_level),
      unknowns: strings(estimate.unknowns),
      unavailable_reason: str(estimate.unavailable_reason),
    },
    plan: Object.fromEntries(Object.entries(rec(d.plan)).map(([k, v]) => [k, num(v)])),
  }
}

export const scopeLabel = (scope: string | null | undefined) => {
  switch (scope) {
    case 'global':
      return 'All S3 storage'
    case 'provider':
      return 'Provider'
    case 'vault':
      return 'Vault'
    case 'gateway_credential':
      return 'Gateway key'
    case 'gateway_credential_vault':
      return 'Gateway key on vault'
    default:
      return scope ?? 'Unknown'
  }
}

export const windowLabel = (window: string | null | undefined) =>
  window === 'monthly' ? 'Monthly'
  : window === 'daily' ? 'Daily'
  : window === 'per_run' ? 'Per run'
  : (window ?? '—')

export const providerLabel = (key: string | null | undefined) =>
  BUDGET_PROVIDERS.find(p => p.key === key)?.label ?? key ?? null
