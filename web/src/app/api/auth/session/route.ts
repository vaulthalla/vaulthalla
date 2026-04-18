import { NextRequest, NextResponse } from 'next/server'

const PREVIEW_ORIGIN = process.env.NEXT_PUBLIC_SERVER_ADDR

export async function GET(req: NextRequest) {
  if (!PREVIEW_ORIGIN) {
    return NextResponse.json({ ok: false, error: 'Missing PREVIEW_ORIGIN' }, { status: 500 })
  }

  const cookieHeader = req.headers.get('cookie') ?? ''

  const upstream = await fetch(`${PREVIEW_ORIGIN}/auth/session`, {
    method: 'GET',
    headers: {
      cookie: cookieHeader,
      // optional but helps if you later gate based on origin/host
      'x-forwarded-host': req.headers.get('host') ?? '',
      'x-forwarded-proto': req.nextUrl.protocol.replace(':', ''),
    },
    cache: 'no-store',
  })

  if (!upstream.ok) {
    return NextResponse.json({ ok: false }, { status: 401 })
  }

  const contentType = upstream.headers.get('content-type') ?? 'application/json'
  const body = await upstream.text()

  return new NextResponse(body, { status: 200, headers: { 'content-type': contentType } })
}
