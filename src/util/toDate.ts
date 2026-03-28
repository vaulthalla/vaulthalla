export const toDate = (value?: number | string | null): Date | null => {
  if (value === null || value === undefined) return null

  if (typeof value === 'number') return new Date(value < 1_000_000_000_000 ? value * 1000 : value)

  const parsed = new Date(value)
  return Number.isNaN(parsed.getTime()) ? null : parsed
}
