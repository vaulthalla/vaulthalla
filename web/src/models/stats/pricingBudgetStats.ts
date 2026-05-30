import { asDecimalString, asNumber, asString, isRecord } from '@/models/pricing/common'
import { PriceBudgetTrendStats } from '@/models/pricing/priceBudget'
import { PriceNotification } from '@/models/pricing/priceNotification'
import { PriceOverride } from '@/models/pricing/priceOverride'

export class PricingBudgetStats {
  active_policies = 0
  blocked_syncs_24h = 0
  warning_notifications = 0
  critical_notifications = 0
  unacknowledged_notifications = 0
  pending_overrides = 0
  current_monthly_spend = '0'
  projected_monthly_spend = '0'
  currency = 'USD'
  trends: PriceBudgetTrendStats[] = []
  active_notifications: PriceNotification[] = []
  recent_overrides: PriceOverride[] = []

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.active_policies = asNumber(data.active_policies)
    this.blocked_syncs_24h = asNumber(data.blocked_syncs_24h)
    this.warning_notifications = asNumber(data.warning_notifications)
    this.critical_notifications = asNumber(data.critical_notifications)
    this.unacknowledged_notifications = asNumber(data.unacknowledged_notifications)
    this.pending_overrides = asNumber(data.pending_overrides)
    this.current_monthly_spend = asDecimalString(data.current_monthly_spend)
    this.projected_monthly_spend = asDecimalString(data.projected_monthly_spend)
    this.currency = asString(data.currency, 'USD')
    this.trends = Array.isArray(data.trends) ? data.trends.map(PriceBudgetTrendStats.from) : []
    this.active_notifications = Array.isArray(data.active_notifications) ? data.active_notifications.map(PriceNotification.from) : []
    this.recent_overrides = Array.isArray(data.recent_overrides) ? data.recent_overrides.map(PriceOverride.from) : []
  }

  static from(input: unknown): PricingBudgetStats {
    return new PricingBudgetStats(input)
  }
}
