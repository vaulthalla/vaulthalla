import { NextRequest, NextResponse } from 'next/server'
import { checkSession } from '@/lib/server/authCheck'

export async function GET(req: NextRequest) {
  const result = await checkSession(
    req.headers.get('cookie'),
    req.headers.get('x-forwarded-host') ?? req.headers.get('host') ?? '',
    req.headers.get('x-forwarded-proto') ?? req.nextUrl.protocol.replace(':', ''),
  )
  if (result.kind === 'ok') return new NextResponse(result.body, { status: 200, headers: { 'content-type': result.contentType } })
  return NextResponse.json(
    { ok: false, authenticated: false, error: result.kind === 'unauthenticated' ? 'unauthenticated' : 'auth_upstream_unavailable' },
    { status: 401 },
  )
}
