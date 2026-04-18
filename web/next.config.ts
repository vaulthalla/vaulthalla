import type { NextConfig } from 'next'

const isTurbo = process.env.NEXT_TURBO === 'true'

const nextConfig: NextConfig = {
  output: 'standalone',
  allowedDevOrigins: ['vh.home.arpa'],
  images: { localPatterns: [{ pathname: '/preview**' }] },
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
