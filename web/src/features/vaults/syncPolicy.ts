import type { ConflictPolicy, S3RequestBudget, SyncPolicy, SyncStrategy } from '@/features/vaults/model'

// S3 sync policy: strategy, conflicts, interval and the per-run request guardrails. The presets are the same numbers
// the console has always offered; `custom` keeps whatever the person typed.

export type BudgetPreset = 'conservative' | 'balanced' | 'bulk' | 'unlimited' | 'custom'
type FixedPreset = Exclude<BudgetPreset, 'custom'>

const GiB = 1024 ** 3

export const BUDGET_PRESETS: Record<FixedPreset, S3RequestBudget> = {
  conservative: {
    list_requests: 10,
    head_requests: 100,
    get_requests: 100,
    put_requests: 100,
    copy_requests: 20,
    delete_requests: 100,
    downloaded_bytes: GiB,
  },
  balanced: {
    list_requests: 100,
    head_requests: 1000,
    get_requests: 1000,
    put_requests: 1000,
    copy_requests: 100,
    delete_requests: 1000,
    downloaded_bytes: 10 * GiB,
  },
  bulk: {
    list_requests: 1000,
    head_requests: 10000,
    get_requests: 10000,
    put_requests: 10000,
    copy_requests: 1000,
    delete_requests: 10000,
    downloaded_bytes: 100 * GiB,
  },
  unlimited: {
    list_requests: null,
    head_requests: null,
    get_requests: null,
    put_requests: null,
    copy_requests: null,
    delete_requests: null,
    downloaded_bytes: null,
  },
}

export const PRESET_OPTIONS: { value: BudgetPreset; label: string; hint: string }[] = [
  { value: 'conservative', label: 'Conservative', hint: 'Small, cheap runs. 1 GiB download cap.' },
  { value: 'balanced', label: 'Balanced', hint: 'The default. 10 GiB download cap.' },
  { value: 'bulk', label: 'Bulk', hint: 'Large migrations. 100 GiB download cap.' },
  { value: 'unlimited', label: 'Unlimited', hint: 'No per-run request limits.' },
  { value: 'custom', label: 'Custom', hint: 'Set each limit yourself.' },
]

export const BUDGET_FIELDS: { key: keyof S3RequestBudget; label: string; bytes?: boolean }[] = [
  { key: 'list_requests', label: 'LIST requests' },
  { key: 'head_requests', label: 'HEAD requests' },
  { key: 'get_requests', label: 'GET requests' },
  { key: 'put_requests', label: 'PUT requests' },
  { key: 'copy_requests', label: 'COPY requests' },
  { key: 'delete_requests', label: 'DELETE requests' },
  { key: 'downloaded_bytes', label: 'Downloaded bytes', bytes: true },
]

export const STRATEGY_OPTIONS: { value: SyncStrategy; label: string; hint: string }[] = [
  { value: 'cache', label: 'Cache', hint: 'Fetch from the bucket on demand and keep a local cache.' },
  { value: 'sync', label: 'Sync', hint: 'Two-way: keep the vault and the bucket in step.' },
  { value: 'mirror', label: 'Mirror', hint: 'One-way: make the bucket match the vault.' },
]

export const CONFLICT_OPTIONS: { value: ConflictPolicy; label: string }[] = [
  { value: 'keep_local', label: 'Keep local' },
  { value: 'keep_remote', label: 'Keep remote' },
  { value: 'keep_newest', label: 'Keep newest' },
  { value: 'ask', label: 'Ask' },
]

export const DEFAULT_INTERVAL_SECONDS = 300
export const DEFAULT_INDEX_AGE_SECONDS = 24 * 60 * 60

export const normalizeBudget = (budget?: Partial<S3RequestBudget> | null): S3RequestBudget => ({
  list_requests: budget?.list_requests ?? null,
  head_requests: budget?.head_requests ?? null,
  get_requests: budget?.get_requests ?? null,
  put_requests: budget?.put_requests ?? null,
  copy_requests: budget?.copy_requests ?? null,
  delete_requests: budget?.delete_requests ?? null,
  downloaded_bytes: budget?.downloaded_bytes ?? null,
})

export const presetFor = (budget: S3RequestBudget): BudgetPreset => {
  for (const preset of ['conservative', 'balanced', 'bulk', 'unlimited'] as const)
    if (BUDGET_FIELDS.every(({ key }) => (budget[key] ?? null) === BUDGET_PRESETS[preset][key])) return preset
  return 'custom'
}

// "10m 0s", "1h", "90", 90 → seconds. Unparseable input falls back to the default interval.
export const parseIntervalSeconds = (value: unknown): number => {
  if (typeof value === 'number' && Number.isFinite(value) && value > 0) return Math.round(value)
  if (typeof value !== 'string') return DEFAULT_INTERVAL_SECONDS
  const trimmed = value.trim()
  if (/^\d+$/.test(trimmed)) return Number(trimmed) || DEFAULT_INTERVAL_SECONDS
  const seconds = [...trimmed.matchAll(/(\d+)\s*([dhms])/gi)].reduce((sum, [, amount, unit]) => {
    const n = Number(amount)
    switch (unit.toLowerCase()) {
      case 'd':
        return sum + n * 86400
      case 'h':
        return sum + n * 3600
      case 'm':
        return sum + n * 60
      default:
        return sum + n
    }
  }, 0)
  return seconds > 0 ? seconds : DEFAULT_INTERVAL_SECONDS
}

export interface SyncFormValues {
  strategy: SyncStrategy
  conflict_policy: ConflictPolicy
  interval_seconds: number
  enabled: boolean
  preset: BudgetPreset
  budget: Record<keyof S3RequestBudget, string>
  // Empty = no age limit.
  max_remote_index_age_seconds: string
}

const toText = (value: number | null) => (value === null ? '' : String(value))

export const syncFormDefaults = (sync?: Partial<SyncPolicy> | null): SyncFormValues => {
  const budget = sync?.s3_request_budget ? normalizeBudget(sync.s3_request_budget) : BUDGET_PRESETS.balanced
  return {
    strategy: sync?.strategy ?? 'cache',
    conflict_policy: sync?.conflict_policy ?? 'keep_local',
    interval_seconds: parseIntervalSeconds(sync?.interval ?? DEFAULT_INTERVAL_SECONDS),
    enabled: sync?.enabled ?? true,
    preset: presetFor(budget),
    budget: Object.fromEntries(BUDGET_FIELDS.map(({ key }) => [key, toText(budget[key])])) as SyncFormValues['budget'],
    max_remote_index_age_seconds:
      sync && sync.max_remote_index_age_seconds === null ? '' : String(sync?.max_remote_index_age_seconds ?? DEFAULT_INDEX_AGE_SECONDS),
  }
}

const nullableCount = (text: string): number | null => {
  const trimmed = text.trim()
  if (!trimmed) return null
  const n = Number(trimmed)
  return Number.isFinite(n) && n >= 0 ? Math.floor(n) : null
}

export const budgetFromForm = (values: Pick<SyncFormValues, 'preset' | 'budget'>): S3RequestBudget =>
  values.preset === 'custom'
    ? (Object.fromEntries(BUDGET_FIELDS.map(({ key }) => [key, nullableCount(values.budget[key])])) as unknown as S3RequestBudget)
    : { ...BUDGET_PRESETS[values.preset] }

// The ws `sync` object. Built field by field; never a spread of what the server sent.
export const syncPayload = (values: SyncFormValues) => ({
  strategy: values.strategy,
  conflict_policy: values.conflict_policy,
  interval: Math.max(1, Math.round(values.interval_seconds || DEFAULT_INTERVAL_SECONDS)),
  enabled: values.enabled,
  s3_request_budget: budgetFromForm(values),
  max_remote_index_age_seconds: nullableCount(values.max_remote_index_age_seconds),
})

export const INTERVAL_CHOICES = [
  { value: 60, label: 'Every minute' },
  { value: 300, label: 'Every 5 minutes' },
  { value: 600, label: 'Every 10 minutes' },
  { value: 900, label: 'Every 15 minutes' },
  { value: 1800, label: 'Every 30 minutes' },
  { value: 3600, label: 'Every hour' },
  { value: 21600, label: 'Every 6 hours' },
  { value: 86400, label: 'Every day' },
]
