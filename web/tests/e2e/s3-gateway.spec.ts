import { expect, test } from '@playwright/test'
import { authStatePath, authenticateAndSaveState, explicitSkipRequested } from './helpers/auth'
import {
  addCredentialOverride,
  assignVaultRole,
  createCredential,
  createLocalBucket,
  ensureVaultAvailable,
  gotoS3Gateway,
  hideSecret,
  removeCredentialOverride,
  revokeVaultRole,
  saveKeyBudget,
  saveKeyVaultBudget,
  selectCredential,
  uniqueE2EName,
} from './helpers/s3Gateway'

test.describe.configure({ mode: 'serial' })
test.skip(explicitSkipRequested(), 'VAULTHALLA_E2E_SKIP requested')
test.use({ storageState: authStatePath })

let userAccessCredential = ''
let vaultAllowCredential = ''

test.beforeAll(async ({ browser }) => {
  await authenticateAndSaveState(browser, authStatePath)
})

async function ensureUserCredential(page: Parameters<typeof gotoS3Gateway>[0]) {
  if (!userAccessCredential) {
    userAccessCredential = uniqueE2EName('pw-user-access')
    await createCredential(page, userAccessCredential)
    await hideSecret(page)
  }
  await selectCredential(page, userAccessCredential)
}

test('admin can navigate to S3 Gateway page', async ({ page }) => {
  await gotoS3Gateway(page)
  await expect(page.getByTestId('s3-gateway-section-service')).toBeVisible()
  await expect(page.getByTestId('s3-gateway-section-credentials')).toBeVisible()
  await expect(page.getByTestId('s3-gateway-section-bucket-bindings')).toBeVisible()
  await expect(page.getByTestId('s3-gateway-section-budgets')).toBeVisible()
  await expect(page.getByTestId('s3-gateway-section-client-setup')).toBeVisible()
})

test('admin can create a user_access credential and hide the secret', async ({ page }) => {
  await gotoS3Gateway(page)
  userAccessCredential = uniqueE2EName('pw-user-access')
  await createCredential(page, userAccessCredential)
  await expect(page.getByTestId('s3-gateway-secret-panel')).toContainText(/secret access key/i)
  await hideSecret(page)
  await expect(page.getByTestId('s3-gateway-credential-name').filter({ hasText: userAccessCredential })).toBeVisible()
})

test('admin manages vault_allowlist roles and overrides through role-native editor', async ({ page }) => {
  await gotoS3Gateway(page)
  const vaultName = await ensureVaultAvailable(page)
  vaultAllowCredential = uniqueE2EName('pw-vault-allow')
  await createCredential(page, vaultAllowCredential)
  await hideSecret(page)
  await selectCredential(page, vaultAllowCredential)

  await page.getByTestId('s3-gateway-scope-editor-scope-select').selectOption('vault_allowlist')
  await page.getByTestId('s3-gateway-scope-save').click()
  await expect(page.getByTestId('s3-gateway-scope-editor-scope-select')).toHaveValue('vault_allowlist')
  await expect(page.getByTestId('s3-gateway-vault-allowlist-empty')).toContainText(/assign at least one vault role/i)

  await assignVaultRole(page, vaultName, 'reader')
  await addCredentialOverride(page, vaultName)
  await removeCredentialOverride(page)
  await revokeVaultRole(page, vaultName)
})

test('credential create modal uses relational principal selector', async ({ page }) => {
  await gotoS3Gateway(page)
  await page.getByTestId('s3-gateway-open-create-credential').click()
  await expect(page.getByTestId('s3-gateway-create-credential-modal')).toBeVisible()
  const selector = page.getByTestId('s3-gateway-credential-principal-select')
  await expect(selector).toBeVisible()
  await expect(page.getByTestId('s3-gateway-create-credential-modal').locator('input[name="principal_user_id"]')).toHaveCount(0)
  await page.getByRole('button', { name: /^close$/i }).click()
})

test('admin can create a local gateway bucket', async ({ page }) => {
  await gotoS3Gateway(page)
  await createLocalBucket(page, uniqueE2EName('pw-local-bucket'))
})

test('admin can set a per-key budget', async ({ page }) => {
  await gotoS3Gateway(page)
  await ensureUserCredential(page)
  await saveKeyBudget(page, '0.25')
})

test('admin can set a per-key/vault budget', async ({ page }) => {
  await gotoS3Gateway(page)
  const vaultName = await ensureVaultAvailable(page)
  await ensureUserCredential(page)
  await saveKeyVaultBudget(page, '0.15', vaultName)
})

test('admin can create and update local budget enforcement on a credential', async ({ page }) => {
  await gotoS3Gateway(page)
  const credentialName = uniqueE2EName('pw-local-budget')
  await createCredential(page, credentialName, 'user_access', undefined, true)
  await hideSecret(page)
  await selectCredential(page, credentialName)
  await expect(page.getByTestId('s3-gateway-scope-enforce-local-budget')).toBeChecked()

  await page.getByTestId('s3-gateway-scope-enforce-local-budget').uncheck()
  await page.getByTestId('s3-gateway-scope-save').click()
  await page.reload()
  await expect(page.getByTestId('s3-gateway-section-service')).toBeVisible()
  await selectCredential(page, credentialName)
  await expect(page.getByTestId('s3-gateway-scope-enforce-local-budget')).not.toBeChecked()
})

test('admin can disable a key budget policy', async ({ page }) => {
  await gotoS3Gateway(page)
  await ensureUserCredential(page)
  await saveKeyBudget(page, '0.37')
  await expect(page.getByTestId('s3-gateway-key-budget-disable')).toBeEnabled()
  await page.getByTestId('s3-gateway-key-budget-disable').click()
  await expect(page.getByTestId('s3-gateway-key-budget-disable')).toBeDisabled()
})

test('budget panel scopes policy controls to the selected credential and shows ledger source column', async ({ page }) => {
  await gotoS3Gateway(page)
  const first = uniqueE2EName('pw-budget-filter-a')
  const second = uniqueE2EName('pw-budget-filter-b')
  await createCredential(page, first)
  await hideSecret(page)
  await createCredential(page, second)
  await hideSecret(page)

  await selectCredential(page, first)
  await saveKeyBudget(page, '0.41')
  await expect(page.getByTestId('s3-gateway-key-budget-disable')).toBeEnabled()

  await selectCredential(page, second)
  await expect(page.getByTestId('s3-gateway-key-budget-disable')).toBeDisabled()
  await expect(page.getByTestId('s3-gateway-section-budgets').getByText('Source')).toBeVisible()
})

test('invalid budget value reports a visible error', async ({ page }) => {
  await gotoS3Gateway(page)
  await ensureUserCredential(page)
  await saveKeyBudget(page, 'not-a-number', false)
  await expect(page.locator('body')).toContainText(/invalid|unable|error/i)
})
