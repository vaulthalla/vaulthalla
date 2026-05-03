import { NextRequest, NextResponse } from 'next/server'

export const config = {
  matcher: ['/((?!login|share|api|_next|favicon.ico|robots.txt|sitemap.xml).*)'],
}

const getInternalAuthOrigin = (req: NextRequest) => {
  return (
    process.env.VAULTHALLA_WEB_INTERNAL_ORIGIN ??
    process.env.NEXT_PRIVATE_WEB_INTERNAL_ORIGIN ??
    (process.env.NODE_ENV === 'production' ? 'http://127.0.0.1:36968' : req.nextUrl.origin)
  )
}

const redirectToLogin = (req: NextRequest) => {
  const redir = req.nextUrl.clone()
  redir.pathname = '/login'
  redir.searchParams.set('next', req.nextUrl.pathname + req.nextUrl.search)
  return NextResponse.redirect(redir)
}

export async function middleware(req: NextRequest) {
  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), 2500)

  try {
    const url = new URL('/api/auth/session', getInternalAuthOrigin(req))

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
