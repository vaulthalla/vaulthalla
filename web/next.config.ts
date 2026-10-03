import type { NextConfig } from 'next'

const isTurbo = process.env.NEXT_TURBO === 'true'

const nextConfig: NextConfig = {
  output: 'standalone',
  devIndicators: false,
  allowedDevOrigins: ['vh.home.arpa'],
  images: { localPatterns: [{ pathname: '/preview**' }] },

  // Old console URLs keep working (bookmarks, links in emails and docs).
  async redirects() {
    return [
      { source: '/fs', destination: '/files', permanent: false },
      { source: '/dashboard', destination: '/health', permanent: false },
      { source: '/dashboard/runtime', destination: '/health/runtime', permanent: false },
      { source: '/dashboard/filesystem', destination: '/health/filesystem', permanent: false },
      { source: '/dashboard/storage', destination: '/health/storage', permanent: false },
      { source: '/dashboard/operations', destination: '/health/activity', permanent: false },
      { source: '/dashboard/trends', destination: '/health/activity', permanent: false },
      { source: '/dashboard/:rest*', destination: '/health', permanent: false },
      { source: '/api-keys', destination: '/credentials', permanent: false },
      { source: '/api-keys/add', destination: '/credentials/new', permanent: false },
      { source: '/api-keys/:id', destination: '/credentials/:id', permanent: false },
      { source: '/pricing-budget', destination: '/cost', permanent: false },
      { source: '/operator-email', destination: '/notifications', permanent: false },
      { source: '/users/add', destination: '/users/new', permanent: false },
      { source: '/users/:name/edit', destination: '/users/:name', permanent: false },
      { source: '/users/:name/change-password', destination: '/users/:name', permanent: false },
      { source: '/vaults/add', destination: '/vaults/new', permanent: false },
      { source: '/vaults/:id/edit', destination: '/vaults/:id/settings', permanent: false },
      { source: '/vaults/:id/assign', destination: '/vaults/:id/access', permanent: false },
      { source: '/roles/admin', destination: '/roles?type=admin', permanent: false },
      { source: '/roles/vault', destination: '/roles?type=vault', permanent: false },
      { source: '/roles/admin/add', destination: '/roles/new?type=admin', permanent: false },
      { source: '/roles/vault/add', destination: '/roles/new?type=vault', permanent: false },
    ]
  },
  turbopack: {
    rules: {
      '*.svg': { loaders: [{ loader: '@svgr/webpack', options: { icon: true, fill: 'currentColor' } }], as: '*.js' },
    },
  },

  webpack(config) {
    if (!isTurbo) {
      config.module.rules.push({
        test: /\.svg$/i,
        issuer: /\.[jt]sx?$/,
        use: [{ loader: '@svgr/webpack', options: { icon: true, fill: 'currentColor' } }],
      })
    }
    return config
  },
}

export default nextConfig
