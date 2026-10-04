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

// Only document loads are checked upstream (each check is a password-strength hash verify in the daemon). RSC
// fetches — router prefetches and client-side navigations — carry no data (every page loads its data over the
// websocket after the session gate). Next strips its own flight headers (RSC, Next-Router-Prefetch) and `_rsc`
// before middleware runs, so tell them apart by Sec-Fetch-Dest: the browser sends `empty` for fetches and
// `document` for page loads. Clients that send no Sec-Fetch-Dest are checked.
const skipsCheck = (req: NextRequest) => {
  if (req.headers.get('purpose') === 'prefetch' || req.headers.get('sec-purpose')?.includes('prefetch')) return true
  const dest = req.headers.get('sec-fetch-dest')
  return dest !== null && dest !== 'document'
}

const redirectToLogin = (req: NextRequest) => {
  const redir = new URL('/login', req.url)
  redir.searchParams.set('next', req.nextUrl.pathname + req.nextUrl.search)
  return NextResponse.redirect(redir)
}

export async function middleware(req: NextRequest) {
  if (isPublic(req) || skipsCheck(req)) return NextResponse.next()

  const result = await checkSession(
    req.headers.get('cookie'),
    req.headers.get('x-forwarded-host') ?? req.headers.get('host') ?? '',
    req.headers.get('x-forwarded-proto') ?? req.nextUrl.protocol.replace(':', ''),
  )
  // A daemon that is down or restarting is not a logged-out user: let the page load and show its reconnect state.
  if (result.kind === 'unauthenticated') return redirectToLogin(req)
  return NextResponse.next()
}
