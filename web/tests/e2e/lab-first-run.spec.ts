import { readFileSync } from 'node:fs'
import { expect, test, type Page } from '@playwright/test'

// Real-host first-run checks for a packaged install (e.g. vh-storage), driven through nginx.
// Opt-in only:
//   VAULTHALLA_E2E_LAB=1 VAULTHALLA_E2E_SKIP=1 VAULTHALLA_E2E_NO_WEB_SERVER=1 \
//   VAULTHALLA_E2E_BASE_URL=http://<host> \
//   VAULTHALLA_E2E_LAB_INITIAL_ADMIN_PASSWORD_FILE=<copy of the host's /var/lib/vaulthalla/super_admin_initial_password> \
//   VAULTHALLA_E2E_LAB_NEW_ADMIN_PASSWORD_FILE=<0600 file> \
//   pnpm exec playwright test tests/e2e/lab-first-run.spec.ts
// There is no universal default password: a fresh install generates one for 'admin' and writes it to the host's
// initial password file. Both passwords are read from files so they never appear in argv or logs.

const labEnabled = process.env.VAULTHALLA_E2E_LAB === '1'

function passwordFrom(variable: string, minLength: number) {
  const file = process.env[variable]
  if (!file) throw new Error(`${variable} is required`)
  const value = readFileSync(file, 'utf8').trim()
  if (value.length < minLength) throw new Error(`${variable} must hold at least ${minLength} characters`)
  return value
}

const initialAdminPassword = () => passwordFrom('VAULTHALLA_E2E_LAB_INITIAL_ADMIN_PASSWORD_FILE', 32)
const newAdminPassword = () => passwordFrom('VAULTHALLA_E2E_LAB_NEW_ADMIN_PASSWORD_FILE', 12)

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

  test('the generated password signs in to a normal session that only warns', async ({ page }) => {
    await login(page, 'admin', initialAdminPassword())
    expect(page.url()).not.toContain('/change-password')
    await expect(page.getByTestId('initial-password-warning')).toBeVisible({ timeout: 15_000 })

    const response = await page.goto('/users')
    expect(response?.status()).toBeLessThan(400)
    expect(page.url()).toContain('/users')
    await expect(page.getByText(/unauthori[sz]ed|internal error|something went wrong/i)).toHaveCount(0)
  })

  test('changing the password works and retires the warning', async ({ page }) => {
    const initial = initialAdminPassword()
    const replacement = newAdminPassword()
    await login(page, 'admin', initial)
    await page.goto('/users/admin/change-password')

    await page.locator('input[autocomplete="current-password"]').fill(initial)
    await page.locator('input[autocomplete="new-password"]').nth(0).fill(replacement)
    await page.locator('input[autocomplete="new-password"]').nth(1).fill(replacement)
    await page.getByRole('button', { name: /change password/i }).click()
    await expect(page.getByText(/error|failed/i)).toHaveCount(0, { timeout: 10_000 })

    await page.context().clearCookies()
    await page.evaluate(() => { localStorage.clear(); sessionStorage.clear() })
    await login(page, 'admin', replacement)
    await expect(page.getByTestId('initial-password-warning')).toHaveCount(0, { timeout: 10_000 })
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
