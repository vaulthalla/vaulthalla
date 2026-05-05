import { NextRequest, NextResponse } from 'next/server'

export const config = {
  matcher: ['/((?!api|_next|favicon.ico|robots.txt|sitemap.xml).*)'],
}

const WEB_ORIGIN_FALLBACK = 'http://127.0.0.1:36968'
const FETCH_TIMEOUT_MS = 2500

const PUBLIC_PATH_PREFIXES = ['/login', '/share']
const PUBLIC_FILES = new Set(['/favicon.ico', '/robots.txt', '/sitemap.xml'])
const STATIC_ASSET_PATTERN = /\.(?:avif|css|gif|ico|jpeg|jpg|js|json|map|png|svg|txt|webmanifest|webp|woff|woff2)$/i

const getInternalWebOrigin = (req: NextRequest) => {
  return (
    process.env.VAULTHALLA_WEB_INTERNAL_ORIGIN ??
    process.env.NEXT_PRIVATE_WEB_INTERNAL_ORIGIN ??
    (process.env.NODE_ENV === 'production' ? WEB_ORIGIN_FALLBACK : req.nextUrl.origin)
  ).replace(/\/+$/, '')
}

const shouldBypassAuth = (req: NextRequest) => {
  const { pathname } = req.nextUrl

  if (PUBLIC_FILES.has(pathname)) return true
  if (pathname.startsWith('/_next') || pathname.startsWith('/api')) return true
  if (PUBLIC_PATH_PREFIXES.some(path => pathname === path || pathname.startsWith(`${path}/`))) return true

  return STATIC_ASSET_PATTERN.test(pathname)
}

const redirectToLogin = (req: NextRequest) => {
  const redir = new URL('/login', req.url)
  redir.searchParams.set('next', req.nextUrl.pathname + req.nextUrl.search)
  return NextResponse.redirect(redir)
}

export async function middleware(req: NextRequest) {
  if (shouldBypassAuth(req)) return NextResponse.next()

  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS)

  try {
    const url = new URL('/api/auth/session', getInternalWebOrigin(req))

    const res = await fetch(url, {
      method: 'GET',
      headers: {
        cookie: req.headers.get('cookie') ?? '',
      },
      cache: 'no-store',
      signal: controller.signal,
    })

    if (!res.ok) return redirectToLogin(req)
    return NextResponse.next()
  } catch {
    return redirectToLogin(req)
  } finally {
    clearTimeout(timeout)
  }
}
