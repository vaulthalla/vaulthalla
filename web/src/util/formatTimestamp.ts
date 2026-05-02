const parseNumericTimestamp = (value: number) => {
  if (!Number.isFinite(value) || value <= 0) return null
  return new Date(value < 1_000_000_000_000 ? value * 1000 : value)
}

export const parseTimestamp = (value?: number | string | null): Date | null => {
  if (value === null || value === undefined) return null

  if (typeof value === 'number') return parseNumericTimestamp(value)

  const trimmed = value.trim()
  if (!trimmed) return null

  if (/^\d+(\.\d+)?$/.test(trimmed)) return parseNumericTimestamp(Number(trimmed))

  const parsed = new Date(trimmed)
  return Number.isNaN(parsed.getTime()) ? null : parsed
}

export const formatTimestamp = (value?: number | string | null, fallback = '—') => {
  const parsed = parseTimestamp(value)
  return parsed ? parsed.toLocaleString() : fallback
}
