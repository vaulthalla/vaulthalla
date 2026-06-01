import { execFileSync } from 'node:child_process'
import { resolve } from 'node:path'

function explicitSkipRequested() {
  return process.env.VAULTHALLA_E2E_SKIP === '1' ||
    process.env.VAULTHALLA_E2E_SKIP === 'true'
}

function decodeProvisionExports(exportsText: string, repoRoot: string) {
  const output = execFileSync('bash', [
    '-lc',
    `${exportsText}\nprintf '%s\\0%s\\0' "$VAULTHALLA_E2E_USER" "$VAULTHALLA_E2E_PASSWORD"`,
  ], {
    cwd: repoRoot,
    env: process.env,
    encoding: 'buffer',
    stdio: ['ignore', 'pipe', 'inherit'],
  }).toString('utf8')

  const [user, password] = output.split('\0')
  if (!user || !password) {
    throw new Error('E2E credential provisioning completed without returning user and password exports.')
  }
  return { user, password }
}

function globalSetup() {
  if (explicitSkipRequested()) return

  const repoRoot = resolve(__dirname, '../../..')
  const provisionScript = resolve(repoRoot, 'tools/e2e/provision_e2e_user.sh')
  const provisionEnv = {
    ...process.env,
    VAULTHALLA_E2E_USER: '',
    VAULTHALLA_E2E_PASSWORD: '',
    VAULTHALLA_E2E_EMAIL: '',
  }
  const exportsText = execFileSync(provisionScript, ['--force', '--fresh-user', '--print-exports'], {
    cwd: repoRoot,
    env: provisionEnv,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'inherit'],
  })
  const { user, password } = decodeProvisionExports(exportsText, repoRoot)

  process.env.VAULTHALLA_E2E_USER = user
  process.env.VAULTHALLA_E2E_PASSWORD = password
}

export default globalSetup
