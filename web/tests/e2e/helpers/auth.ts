import { expect, type Browser, type Page } from '@playwright/test'
import { mkdir } from 'node:fs/promises'
import { dirname } from 'node:path'

export const authStatePath = 'test-results/.auth/s3-gateway.json'

export function explicitSkipRequested() {
  return process.env.VAULTHALLA_E2E_SKIP === '1' ||
    process.env.VAULTHALLA_E2E_SKIP === 'true'
}

export function e2eCredentials() {
  const user = process.env.VAULTHALLA_E2E_USER
  const password = process.env.VAULTHALLA_E2E_PASSWORD
  if (!user || !password) {
    throw new Error(
      'Missing E2E credentials. Run tools/e2e/provision_e2e_user.sh or set VAULTHALLA_E2E_USER and VAULTHALLA_E2E_PASSWORD. Set VAULTHALLA_E2E_SKIP=1 only to skip explicitly.',
    )
  }
  return { user, password }
}

// The console's sign-in form (labelled fields; the session cookie is HttpOnly, the access token lives in memory).
export async function signIn(page: Page, user: string, password: string) {
  await page.goto('/login')
  await expect(page.getByRole('heading', { name: /sign in to vaulthalla/i })).toBeVisible()
  await page.getByLabel('Username').fill(user)
  await page.getByLabel('Password').fill(password)
  await page.getByRole('button', { name: /^sign in$/i }).click()
  await page.waitForURL(url => !url.pathname.endsWith('/login'), { timeout: 15_000 })
}

export async function loginThroughUi(page: Page) {
  const { user, password } = e2eCredentials()
  await signIn(page, user, password)
}

export async function authenticateAndSaveState(browser: Browser, storageStatePath = authStatePath) {
  await mkdir(dirname(storageStatePath), { recursive: true })
  const context = await browser.newContext({ storageState: undefined })
  const page = await context.newPage()
  try {
    await loginThroughUi(page)
    await context.storageState({ path: storageStatePath })
  } finally {
    await context.close()
  }
}
