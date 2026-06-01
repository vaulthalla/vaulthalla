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

export async function loginThroughUi(page: Page) {
  const { user, password } = e2eCredentials()
  await page.goto('/login')
  await expect(page.getByRole('heading', { name: /login to vaulthalla/i })).toBeVisible()
  await page.getByPlaceholder('Enter your username').fill(user)
  await page.getByPlaceholder('Enter your password').fill(password)
  await page.getByRole('button', { name: /^login$/i }).click()
  await page.waitForURL(url => !url.pathname.endsWith('/login'), { timeout: 15_000 })
  if (page.url().includes('/change-password')) {
    throw new Error('E2E login reached the forced password-change page. Provide a seeded non-expiring E2E admin user.')
  }
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
