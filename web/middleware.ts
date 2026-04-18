import { NextRequest, NextResponse } from 'next/server'

export const config = { matcher: ['/((?!login|api|_next|favicon.ico|robots.txt|sitemap.xml).*)'] }

export async function middleware(req: NextRequest) {
  const url = new URL('/api/auth/session', req.url)

  const res = await fetch(url, {
    method: 'GET',
    headers: { cookie: req.headers.get('cookie') ?? '' },
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
