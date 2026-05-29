export type UnknownRecord = Record<string, unknown>

export function isRecord(value: unknown): value is UnknownRecord {
  return !!value && typeof value === 'object' && !Array.isArray(value)
}

export function asString(value: unknown, fallback = ''): string {
  if (typeof value === 'string') return value
  if (typeof value === 'number' && Number.isFinite(value)) return String(value)
  return fallback
}

export function asNullableString(value: unknown): string | null {
  if (value == null) return null
  const text = asString(value).trim()
  return text.length > 0 ? text : null
}

export function asNumber(value: unknown, fallback = 0): number {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  if (typeof value === 'string' && value.trim() !== '') {
    const parsed = Number(value)
    if (Number.isFinite(parsed)) return parsed
  }
  return fallback
}

export function asNullableNumber(value: unknown): number | null {
  if (value == null) return null
  const parsed = asNumber(value, Number.NaN)
  return Number.isFinite(parsed) ? parsed : null
}

export function asBoolean(value: unknown, fallback = false): boolean {
  return typeof value === 'boolean' ? value : fallback
}

export function asDateValue(value: unknown): number | string | null {
  if (typeof value === 'string' && value.length > 0) return value
  if (typeof value === 'number' && Number.isFinite(value)) return value
  return null
}

export function asDecimalString(value: unknown, fallback = '0'): string {
  if (typeof value === 'string' && value.trim() !== '') return value
  if (typeof value === 'number' && Number.isFinite(value)) return String(value)
  return fallback
}

export function asNullableDecimalString(value: unknown): string | null {
  if (value == null) return null
  const text = asDecimalString(value, '').trim()
  return text.length > 0 ? text : null
}

export function asStringArray(value: unknown): string[] {
  return Array.isArray(value) ? value.map(item => asString(item)).filter(Boolean) : []
}
