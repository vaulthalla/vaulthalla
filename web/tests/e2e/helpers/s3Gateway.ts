import { expect, type Page } from '@playwright/test'

export function uniqueE2EName(prefix: string) {
  return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`
}

export async function gotoS3Gateway(page: Page) {
  await page.goto('/s3-gateway')
  await expect(page.getByTestId('s3-gateway-section-service')).toBeVisible()
}

export async function ensureVaultAvailable(page: Page) {
  const vaultName = uniqueE2EName('pw-vault-seed')
  await createLocalBucket(page, vaultName)
  await page.reload()
  await expect(page.getByTestId('s3-gateway-section-service')).toBeVisible()
  return vaultName
}

export async function createCredential(
  page: Page,
  name: string,
  scope: 'user_access' | 'vault_allowlist' = 'user_access',
  vaultName?: string,
  enforceLocalBudget = false,
) {
  await page.getByTestId('s3-gateway-open-create-credential').click()
  await expect(page.getByTestId('s3-gateway-create-credential-modal')).toBeVisible()
  await page.getByTestId('s3-gateway-credential-name-input').fill(name)
  await page.getByTestId('s3-gateway-credential-scope-select').selectOption(scope)
  if (enforceLocalBudget) {
    await page.getByTestId('s3-gateway-create-enforce-local-budget').check()
  }
  await page.getByTestId('s3-gateway-submit-create-credential').click()
  await expect(page.getByTestId('s3-gateway-secret-panel')).toBeVisible()
  await expect(page.getByTestId('s3-gateway-credential-name').filter({ hasText: name })).toBeVisible()
}

async function optionValueByText(select: ReturnType<Page['getByTestId']>, text: string) {
  const option = select.locator('option').filter({ hasText: text }).first()
  await expect(option).toHaveCount(1)
  const value = await option.getAttribute('value')
  if (!value) throw new Error(`No selectable option found for ${text}`)
  return value
}

export async function assignVaultRole(page: Page, vaultName: string, roleName = 'reader') {
  const vaultSelect = page.getByTestId('s3-gateway-role-assign-vault-select')
  await vaultSelect.selectOption(await optionValueByText(vaultSelect, vaultName))
  await page.getByTestId('s3-gateway-role-assign-role-select').selectOption({ label: roleName })
  await page.getByTestId('s3-gateway-role-assign-submit').click()
  await expect(page.getByTestId('s3-gateway-role-assignment-row').filter({ hasText: vaultName })).toBeVisible()
}

export async function addCredentialOverride(page: Page, vaultName: string, glob = '/private/**') {
  const vaultSelect = page.getByTestId('s3-gateway-override-vault-select')
  await vaultSelect.selectOption(await optionValueByText(vaultSelect, vaultName))
  await page.getByTestId('s3-gateway-override-permission-select').selectOption('vault.fs.files.download')
  await page.getByTestId('s3-gateway-override-glob-input').fill(glob)
  await page.getByTestId('s3-gateway-override-effect-select').selectOption('deny')
  await page.getByTestId('s3-gateway-override-add-submit').click()
  await expect(page.getByTestId('s3-gateway-section-credential-roles')).toContainText(glob)
}

export async function removeCredentialOverride(page: Page, glob = '/private/**') {
  const row = page.locator('tbody tr').filter({ hasText: glob }).first()
  await expect(row).toBeVisible()
  await row.getByRole('button', { name: /remove/i }).click()
  await expect(row).toBeHidden()
}

export async function revokeVaultRole(page: Page, vaultName: string) {
  const row = page.getByTestId('s3-gateway-role-assignment-row').filter({ hasText: vaultName }).first()
  await expect(row).toBeVisible()
  await row.getByRole('button', { name: /revoke/i }).click()
  await expect(row).toBeHidden()
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

export async function saveKeyBudget(page: Page, amount: string, waitForPolicy = true) {
  await page.getByTestId('s3-gateway-key-budget-input').fill(amount)
  await page.getByTestId('s3-gateway-key-budget-save').click()
  if (waitForPolicy) {
    await expect(page.getByTestId('s3-gateway-section-budgets')).toContainText('gateway_credential')
  }
}

export async function saveKeyVaultBudget(page: Page, amount: string, vaultName?: string) {
  const vaultSelect = page.getByTestId('s3-gateway-key-vault-budget-vault-select')
  const optionCount = await vaultSelect.locator('option').count()
  if (optionCount < 2) throw new Error('S3 Gateway E2E requires at least one vault for key/vault budget tests.')
  if (vaultName) {
    await vaultSelect.selectOption({ label: vaultName })
  } else {
    const value = await vaultSelect.locator('option').nth(1).getAttribute('value')
    if (!value) throw new Error('First key/vault budget option has no vault id.')
    await vaultSelect.selectOption(value)
  }
  await page.getByTestId('s3-gateway-key-vault-budget-input').fill(amount)
  await page.getByTestId('s3-gateway-key-vault-budget-save').click()
  await expect(page.getByTestId('s3-gateway-section-budgets')).toContainText('gateway_credential_vault')
}
