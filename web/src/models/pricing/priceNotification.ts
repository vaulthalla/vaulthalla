import {
  asDateValue,
  asNullableNumber,
  asNullableString,
  asNumber,
  asString,
  isRecord,
  type UnknownRecord,
} from '@/models/pricing/common'

export type PriceNotificationSeverity = 'info' | 'warning' | 'error' | 'critical'

function asSeverity(value: unknown): PriceNotificationSeverity {
  return value === 'warning' || value === 'error' || value === 'critical' ? value : 'info'
}

export class PriceNotification {
  id = 0
  type = ''
  severity: PriceNotificationSeverity = 'info'
  title = ''
  message = ''
  scope: string | null = null
  vault_id: number | null = null
  provider_key: string | null = null
  policy_id: number | null = null
  run_uuid: string | null = null
  metadata: UnknownRecord = {}
  acknowledged_at: number | string | null = null
  acknowledged_by: number | null = null
  created_at: number | string | null = null
  expires_at: number | string | null = null

  constructor(input: unknown = {}) {
    const data = isRecord(input) ? input : {}
    this.id = asNumber(data.id)
    this.type = asString(data.type)
    this.severity = asSeverity(data.severity)
    this.title = asString(data.title)
    this.message = asString(data.message)
    this.scope = asNullableString(data.scope)
    this.vault_id = asNullableNumber(data.vault_id)
    this.provider_key = asNullableString(data.provider_key)
    this.policy_id = asNullableNumber(data.policy_id)
    this.run_uuid = asNullableString(data.run_uuid)
    this.metadata = isRecord(data.metadata) ? data.metadata : {}
    this.acknowledged_at = asDateValue(data.acknowledged_at)
    this.acknowledged_by = asNullableNumber(data.acknowledged_by)
    this.created_at = asDateValue(data.created_at)
    this.expires_at = asDateValue(data.expires_at)
  }

  static from(input: unknown): PriceNotification {
    return new PriceNotification(input)
  }

  isAcknowledged(): boolean {
    return this.acknowledged_at != null
  }
}
