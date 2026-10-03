import { expect, test, type Browser, type Page } from '@playwright/test'
import { randomBytes } from 'node:crypto'
import { authStatePath, authenticateAndSaveState, explicitSkipRequested, signIn } from './helpers/auth'

// RBAC personas through the console: the super admin creates each persona in Users → New user, the persona signs in
// in a fresh browser, and the console must offer exactly what core allows — no admin links a role can't use, a typed
// "no access" state (never a spinner) on direct URLs, and no admin-only traffic. Each persona is deleted afterwards.
//   VAULTHALLA_E2E_BASE_URL=https://localhost VAULTHALLA_E2E_NO_WEB_SERVER=1 pnpm exec playwright test tests/e2e/personas.spec.ts

test.skip(explicitSkipRequested(), 'VAULTHALLA_E2E_SKIP set')
test.use({ storageState: authStatePath, ignoreHTTPSErrors: true })

test.beforeAll(async ({ browser }) => {
  await authenticateAndSaveState(browser)
})

const RUN = Date.now().toString(36)

const createPersona = async (page: Page, role: string) => {
  const name = `zz_${role}_${RUN}`.slice(0, 40)
  const password = `${randomBytes(12).toString('base64url')}-Aa1!`
  await page.goto('/users/new')
  await page.getByLabel('Username').fill(name)
  await page.getByLabel('Password', { exact: true }).fill(password)
  await page.getByLabel('Confirm password').fill(password)
  await page.getByLabel('Admin role').selectOption(role)
  await page.getByRole('button', { name: 'Create user' }).click()
  await page.waitForURL(new RegExp(`/users/${name}$`), { timeout: 15_000 })
  return { name, password }
}

const deletePersona = async (page: Page, name: string) => {
  await page.goto(`/users/${encodeURIComponent(name)}`)
  await page.getByRole('button', { name: /delete user/i }).click()
  const confirmButton = page.getByRole('dialog').getByRole('button', { name: 'Delete user', exact: true })
  await expect(confirmButton).toBeEnabled({ timeout: 15_000 })
  await confirmButton.click()
  await page.waitForURL(/\/users$/, { timeout: 15_000 })
}

const asPersona = async (browser: Browser, persona: { name: string; password: string }) => {
  const context = await browser.newContext({ ignoreHTTPSErrors: true, storageState: undefined })
  const page = await context.newPage()
  const sent: string[] = []
  page.on('websocket', ws => {
    if (!/\/ws$/.test(new URL(ws.url()).pathname)) return
    ws.on('framesent', frame => {
      if (typeof frame.payload !== 'string') return
      try {
        sent.push(JSON.parse(frame.payload).command)
      } catch {
        // not a command frame
      }
    })
  })
  await signIn(page, persona.name, persona.password)
  return { context, page, sent }
}

const navLabels = async (page: Page) => (await page.getByRole('navigation', { name: 'Main' }).getByRole('link').allInnerTexts()).map(t => t.trim()).filter(Boolean)

test.describe.serial('personas', () => {
  test('a user without admin permissions gets Files and Shares only, and plain denials elsewhere', async ({ page, browser }) => {
    const persona = await createPersona(page, 'unprivileged')
    try {
      const { context, page: p, sent } = await asPersona(browser, persona)
      await expect(p).toHaveURL(/\/files/)
      expect(await navLabels(p)).toEqual(['Files', 'Shares'])
      for (const route of ['/users', '/vaults', '/health', '/settings', '/cost', '/credentials', '/roles']) {
        await p.goto(route)
        await expect(p.getByText(/you don.t have access to this/i), route).toBeVisible({ timeout: 15_000 })
        await expect(p.getByRole('status', { name: /loading/i }), route).toHaveCount(0)
      }
      // No admin-only traffic and no client-invented error badge for this persona.
      expect(sent.filter(c => c.startsWith('stats.') || c === 'pricing.notifications.list')).toEqual([])
      await p.goto('/account')
      await expect(p.getByRole('heading', { level: 1 }).first()).toBeVisible()
      await context.close()
    } finally {
      await deletePersona(page, persona.name)
    }
  })

  test('a plain admin sees administration but not the super-admin areas', async ({ page, browser }) => {
    const persona = await createPersona(page, 'admin')
    try {
      const { context, page: p } = await asPersona(browser, persona)
      const labels = await navLabels(p)
      for (const label of ['Files', 'Vaults', 'Users', 'Health']) expect(labels, label).toContain(label)
      for (const label of ['Settings', 'Cost control', 'Notifications']) expect(labels, label).not.toContain(label)
      await p.goto('/health')
      await expect(p.getByText(/you don.t have access to this/i)).toHaveCount(0)
      await p.goto('/settings')
      await expect(p.getByText(/you don.t have access to this/i)).toBeVisible({ timeout: 15_000 })
      await context.close()
    } finally {
      await deletePersona(page, persona.name)
    }
  })

  test('an inactive user cannot sign in', async ({ page, browser }) => {
    const name = `zz_inactive_${RUN}`
    const password = `${randomBytes(12).toString('base64url')}-Aa1!`
    await page.goto('/users/new')
    await page.getByLabel('Username').fill(name)
    await page.getByLabel('Password', { exact: true }).fill(password)
    await page.getByLabel('Confirm password').fill(password)
    await page.getByLabel('Admin role').selectOption('unprivileged')
    await page.getByRole('switch', { name: 'Active' }).click()
    await page.getByRole('button', { name: 'Create inactive user' }).click()
    await page.waitForURL(new RegExp(`/users/${name}$`), { timeout: 15_000 })
    try {
      const context = await browser.newContext({ ignoreHTTPSErrors: true, storageState: undefined })
      const p = await context.newPage()
      await p.goto('/login')
      await p.getByLabel('Username').fill(name)
      await p.getByLabel('Password').fill(password)
      await p.getByRole('button', { name: /^sign in$/i }).click()
      await expect(p.getByRole('alert')).toBeVisible({ timeout: 15_000 })
      await expect(p).toHaveURL(/\/login/)
      await context.close()
    } finally {
      await deletePersona(page, name)
    }
  })
})
