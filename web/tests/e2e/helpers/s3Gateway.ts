import { expect, type Page } from '@playwright/test'

export function uniqueE2EName(prefix: string) {
  return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`
}

export async function gotoS3Gateway(page: Page) {
  await page.goto('/s3-gateway')
  await expect(page.getByTestId('s3-gateway-section-service')).toBeVisible()
}

export async function createCredential(page: Page, name: string, scope: 'user_access' | 'vault_allowlist' = 'user_access') {
  await page.getByTestId('s3-gateway-open-create-credential').click()
  await expect(page.getByTestId('s3-gateway-create-credential-modal')).toBeVisible()
  await page.getByTestId('s3-gateway-credential-name-input').fill(name)
  await page.getByTestId('s3-gateway-credential-scope-select').selectOption(scope)
  if (scope === 'vault_allowlist') {
    const vaultCheckboxes = page.getByTestId('s3-gateway-create-vault-checkbox')
    const count = await vaultCheckboxes.count()
    if (count === 0) throw new Error('S3 Gateway E2E requires at least one vault for vault_allowlist scope tests.')
    await vaultCheckboxes.first().check()
  }
  await page.getByTestId('s3-gateway-submit-create-credential').click()
  await expect(page.getByTestId('s3-gateway-secret-panel')).toBeVisible()
  await expect(page.getByTestId('s3-gateway-credential-name').filter({ hasText: name })).toBeVisible()
}

export async function hideSecret(page: Page) {
  await page.getByTestId('s3-gateway-hide-secret').click()
  await expect(page.getByTestId('s3-gateway-secret-panel')).toBeHidden()
}

export async function selectCredential(page: Page, name: string) {
  const row = page.locator('tbody tr').filter({ has: page.getByTestId('s3-gateway-credential-name').filter({ hasText: name }) }).first()
  await expect(row).toBeVisible()
  await row.getByRole('button', { name: /select/i }).click()
}

export async function createLocalBucket(page: Page, name: string) {
  await page.getByTestId('s3-gateway-local-bucket-name-input').fill(name)
  await page.getByTestId('s3-gateway-create-local-bucket').click()
  await expect(page.getByTestId('s3-gateway-bucket-name').filter({ hasText: name })).toBeVisible()
}

export async function saveKeyBudget(page: Page, amount: string) {
  await page.getByTestId('s3-gateway-key-budget-input').fill(amount)
  await page.getByTestId('s3-gateway-key-budget-save').click()
}

export async function saveKeyVaultBudget(page: Page, amount: string) {
  const vaultSelect = page.getByTestId('s3-gateway-key-vault-budget-vault-select')
  const optionCount = await vaultSelect.locator('option').count()
  if (optionCount < 2) throw new Error('S3 Gateway E2E requires at least one vault for key/vault budget tests.')
  const value = await vaultSelect.locator('option').nth(1).getAttribute('value')
  if (!value) throw new Error('First key/vault budget option has no vault id.')
  await vaultSelect.selectOption(value)
  await page.getByTestId('s3-gateway-key-vault-budget-input').fill(amount)
  await page.getByTestId('s3-gateway-key-vault-budget-save').click()
}
