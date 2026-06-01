import { defineConfig, devices } from '@playwright/test'

const baseURL = process.env.VAULTHALLA_E2E_BASE_URL ?? 'http://127.0.0.1:3000'
const parsedBaseURL = new URL(baseURL)
const localHosts = new Set(['localhost', '127.0.0.1', '::1'])
const shouldStartWebServer = localHosts.has(parsedBaseURL.hostname) && !process.env.VAULTHALLA_E2E_NO_WEB_SERVER
const webServerHost = parsedBaseURL.hostname === 'localhost' ? '127.0.0.1' : parsedBaseURL.hostname
const webServerPort = parsedBaseURL.port || (parsedBaseURL.protocol === 'https:' ? '443' : '80')
const shellArg = (value: string) => JSON.stringify(value)
const webServerCommand = [
  `VAULTHALLA_E2E_WEB_HOST=${shellArg(webServerHost)}`,
  `VAULTHALLA_E2E_WEB_PORT=${shellArg(webServerPort)}`,
  'pnpm run dev:e2e',
].join(' ')

export default defineConfig({
  testDir: './tests/e2e',
  fullyParallel: false,
  timeout: 45_000,
  expect: {
    timeout: 10_000,
  },
  use: {
    baseURL,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'retain-on-failure',
  },
  reporter: [['list'], ['html', { open: 'never' }]],
  ...(shouldStartWebServer ?
    {
      webServer: {
        command: webServerCommand,
        cwd: __dirname,
        url: baseURL,
        reuseExistingServer: true,
        timeout: 120_000,
        stdout: 'pipe',
        stderr: 'pipe',
      },
    }
  : {}),
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
})
