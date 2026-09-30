import { readFileSync } from 'node:fs'
import { expect, test, type Page } from '@playwright/test'

// Real-host first-run checks for a packaged install (e.g. vh-storage), driven through nginx.
// Opt-in only:
//   VAULTHALLA_E2E_LAB=1 VAULTHALLA_E2E_SKIP=1 VAULTHALLA_E2E_NO_WEB_SERVER=1 \
//   VAULTHALLA_E2E_BASE_URL=http://<host> VAULTHALLA_E2E_LAB_NEW_ADMIN_PASSWORD_FILE=<0600 file> \
//   pnpm exec playwright test tests/e2e/lab-first-run.spec.ts
// The default admin password is the package's documented deterministic default. The new password is
// read from a file so it never appears in argv or logs.

const DEFAULT_ADMIN_PASSWORD = 'vh!adm1n'
const labEnabled = process.env.VAULTHALLA_E2E_LAB === '1'

function newAdminPassword() {
  const file = process.env.VAULTHALLA_E2E_LAB_NEW_ADMIN_PASSWORD_FILE
  if (!file) throw new Error('VAULTHALLA_E2E_LAB_NEW_ADMIN_PASSWORD_FILE is required')
  const value = readFileSync(file, 'utf8').trim()
  if (value.length < 12) throw new Error('new admin password must be at least 12 characters')
  return value
}

async function login(page: Page, user: string, password: string) {
  await page.goto('/login')
  await expect(page.getByRole('heading', { name: /login to vaulthalla/i })).toBeVisible()
  await page.getByPlaceholder('Enter your username').fill(user)
  await page.getByPlaceholder('Enter your password').fill(password)
  await page.getByRole('button', { name: /^login$/i }).click()
  await page.waitForURL(url => !url.pathname.endsWith('/login'), { timeout: 15_000 })
}

test.describe.serial('packaged install first run (lab)', () => {
  test.skip(!labEnabled, 'set VAULTHALLA_E2E_LAB=1 to run against a real packaged host')

  test('console is served through nginx, not the distro default site', async ({ page }) => {
    const response = await page.goto('/login')
    expect(response?.status()).toBe(200)
    await expect(page).not.toHaveTitle(/welcome to nginx/i)
    await expect(page.getByRole('heading', { name: /login to vaulthalla/i })).toBeVisible()
  })

  test('default admin password forces a password change, then the new password works', async ({ page }) => {
    const replacement = newAdminPassword()
    await login(page, 'admin', DEFAULT_ADMIN_PASSWORD)
    await page.waitForURL(/\/users\/admin\/change-password/, { timeout: 15_000 })

    await page.locator('input[autocomplete="current-password"]').fill(DEFAULT_ADMIN_PASSWORD)
    await page.locator('input[autocomplete="new-password"]').nth(0).fill(replacement)
    await page.locator('input[autocomplete="new-password"]').nth(1).fill(replacement)
    await page.getByRole('button', { name: /change password/i }).click()
    await expect(page.getByText(/error|failed/i)).toHaveCount(0, { timeout: 10_000 })

    await page.context().clearCookies()
    await page.evaluate(() => { localStorage.clear(); sessionStorage.clear() })
    await login(page, 'admin', replacement)
    expect(page.url()).not.toContain('/change-password')
  })

  test('admin pages render live data over the websocket', async ({ page }) => {
    await login(page, 'admin', newAdminPassword())
    for (const path of ['/vaults', '/users', '/groups', '/roles', '/api-keys', '/settings']) {
      const response = await page.goto(path)
      expect(response?.status(), path).toBeLessThan(400)
      await expect(page.getByText(/unauthori[sz]ed|internal error|something went wrong/i), path).toHaveCount(0)
    }
    await page.goto('/vaults')
    await expect(page.getByText(/admin default vault/i).first()).toBeVisible({ timeout: 15_000 })
  })
})
