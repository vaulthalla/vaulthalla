import { NextRequest, NextResponse } from 'next/server'
import { checkSession } from '@/lib/server/authCheck'

export const config = {
  matcher: ['/((?!api|_next|favicon.ico|robots.txt|sitemap.xml).*)'],
}

const PUBLIC_PATH_PREFIXES = ['/login', '/share']
const PUBLIC_FILES = new Set(['/favicon.ico', '/robots.txt', '/sitemap.xml'])
const STATIC_ASSET_PATTERN = /\.(?:avif|css|gif|ico|jpeg|jpg|js|json|map|png|svg|txt|webmanifest|webp|woff|woff2)$/i

const isPublic = (req: NextRequest) => {
  const { pathname } = req.nextUrl
  if (PUBLIC_FILES.has(pathname)) return true
  if (pathname.startsWith('/_next') || pathname.startsWith('/api')) return true
  if (PUBLIC_PATH_PREFIXES.some(path => pathname === path || pathname.startsWith(`${path}/`))) return true
  return STATIC_ASSET_PATTERN.test(pathname)
}

// Router prefetches carry no data (every page loads its data over the websocket after the session gate), so they
// skip the upstream check; real navigations are checked.
const isPrefetch = (req: NextRequest) =>
  req.headers.get('next-router-prefetch') === '1' || req.headers.get('purpose') === 'prefetch' || req.headers.get('sec-purpose')?.includes('prefetch')

const redirectToLogin = (req: NextRequest) => {
  const redir = new URL('/login', req.url)
  redir.searchParams.set('next', req.nextUrl.pathname + req.nextUrl.search)
  return NextResponse.redirect(redir)
}

export async function middleware(req: NextRequest) {
  if (isPublic(req) || isPrefetch(req)) return NextResponse.next()

  const result = await checkSession(
    req.headers.get('cookie'),
    req.headers.get('x-forwarded-host') ?? req.headers.get('host') ?? '',
    req.headers.get('x-forwarded-proto') ?? req.nextUrl.protocol.replace(':', ''),
  )
  // A daemon that is down or restarting is not a logged-out user: let the page load and show its reconnect state.
  if (result.kind === 'unauthenticated') return redirectToLogin(req)
  return NextResponse.next()
}
