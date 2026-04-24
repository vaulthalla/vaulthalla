import { NextRequest, NextResponse } from 'next/server'

export const config = {
  matcher: ['/((?!login|api|_next|favicon.ico|robots.txt|sitemap.xml).*)'],
}

const getInternalAuthOrigin = (req: NextRequest) => {
  return (
    process.env.VAULTHALLA_WEB_INTERNAL_ORIGIN ??
    process.env.NEXT_PRIVATE_WEB_INTERNAL_ORIGIN ??
    (process.env.NODE_ENV === 'production' ? 'http://127.0.0.1:36968' : req.nextUrl.origin)
  )
}

export async function middleware(req: NextRequest) {
  const url = new URL('/api/auth/session', getInternalAuthOrigin(req))

  const res = await fetch(url, {
    method: 'GET',
    headers: {
      cookie: req.headers.get('cookie') ?? '',
      host: req.headers.get('host') ?? '',
      'x-forwarded-proto': req.headers.get('x-forwarded-proto') ?? req.nextUrl.protocol.replace(':', ''),
      'x-forwarded-host': req.headers.get('x-forwarded-host') ?? req.headers.get('host') ?? '',
    },
    cache: 'no-store',
  })

  if (!res.ok) {
    const redir = req.nextUrl.clone()
    redir.pathname = '/login'
    redir.searchParams.set('next', req.nextUrl.pathname + req.nextUrl.search)
    return NextResponse.redirect(redir)
  }

  return NextResponse.next()
}
