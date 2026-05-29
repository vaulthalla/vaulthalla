import {
  asDateValue,
  asDecimalString,
  asNullableDecimalString,
  asNullableNumber,
  asNullableString,
  asNumber,
  asString,
  isRecord,
} from '@/models/pricing/common'
import {
  type PriceBudgetWindow,
} from '@/models/pricing/priceBudget'

export class PriceBudgetLedgerEntry {
  id: number | null = null
  policy_id: number | null = null
  run_uuid: string | null = null
  vault_id = 0
  provider_key = ''
  currency = 'USD'
  window: PriceBudgetWindow = 'run'
  window_start: number | string | null = null
  window_end: number | string | null = null
  reserved_cost = '0'
  committed_cost: string | null = null
  status = ''
  created_at: number | string | null = null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.id = asNullableNumber(data.id)
    this.policy_id = asNullableNumber(data.policy_id)
    this.run_uuid = asNullableString(data.run_uuid)
    this.vault_id = asNumber(data.vault_id)
    this.provider_key = asString(data.provider_key)
    this.currency = asString(data.currency, 'USD')
    this.window = data.window === 'day' || data.window === 'month' ? data.window : 'run'
    this.window_start = asDateValue(data.window_start)
    this.window_end = asDateValue(data.window_end)
    this.reserved_cost = asDecimalString(data.reserved_cost)
    this.committed_cost = asNullableDecimalString(data.committed_cost)
    this.status = asString(data.status)
    this.created_at = asDateValue(data.created_at)
  }

  static from(input: unknown): PriceBudgetLedgerEntry {
    return new PriceBudgetLedgerEntry(input)
  }
}
