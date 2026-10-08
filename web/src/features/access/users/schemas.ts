import { z } from 'zod'

// Client-side checks mirror core's registration Validator (names 3–50, emails with '@' and '.', passwords 8–128 with
// a letter and a digit below 20 characters). Strength, dictionary and breach checks stay on the server; its message is
// shown as-is.

export const nameSchema = z
  .string()
  .trim()
  .min(3, 'At least 3 characters')
  .max(50, 'At most 50 characters')

export const emailSchema = z
  .string()
  .trim()
  .refine(value => !value || (value.includes('@') && value.includes('.')), 'Enter a valid email address')

export const passwordRule = (value: string): string | null => {
  if (value.length < 8) return 'At least 8 characters'
  if (value.length > 128) return 'At most 128 characters'
  if (value.length < 20 && (!/[0-9]/.test(value) || !/[A-Za-z]/.test(value)))
    return 'Use letters and at least one digit, or 20+ characters'
  return null
}

// superRefine helper: validates `password` and that `confirm` matches it.
export const passwordPair =
  <K extends string, C extends string>(password: K, confirm: C) =>
  (values: Record<K | C, string>, ctx: z.RefinementCtx) => {
    const problem = passwordRule(values[password])
    if (problem) ctx.addIssue({ code: z.ZodIssueCode.custom, path: [password], message: problem })
    if (values[confirm] !== values[password]) ctx.addIssue({ code: z.ZodIssueCode.custom, path: [confirm], message: 'Passwords don’t match' })
  }
