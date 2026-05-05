import { NextRequest, NextResponse } from 'next/server'

const AUTH_ORIGIN_FALLBACK = 'http://127.0.0.1:36970'
const FETCH_TIMEOUT_MS = 2500
const UNAUTHENTICATED_BODY_MARKERS = [
  'refresh token not set',
  'unauthenticated',
  'unauthorized',
  'invalid refresh token',
  'no valid refresh token',
  'no refresh token',
]

type AuthFailureKind = 'unauthenticated' | 'upstream_error'

const resolveAuthOrigin = () =>
  (
    process.env.VAULTHALLA_AUTH_ORIGIN ??
    process.env.VAULTHALLA_PREVIEW_ORIGIN ??
    AUTH_ORIGIN_FALLBACK
  ).replace(/\/+$/, '')

const authSessionUrl = () => new URL('/auth/session', resolveAuthOrigin())

const hasRefreshCookie = (cookieHeader: string | null) => {
  if (!cookieHeader) return false
  return cookieHeader.split(';').some(cookie => cookie.trim().startsWith('refresh='))
}

const classifyAuthFailure = (status: number, body: string): AuthFailureKind => {
  if (status === 401 || status === 403) return 'unauthenticated'

  const normalized = body.toLowerCase()
  if (UNAUTHENTICATED_BODY_MARKERS.some(marker => normalized.includes(marker))) return 'unauthenticated'

  return 'upstream_error'
}

const authFailureResponse = (error: 'unauthenticated' | 'auth_upstream_unavailable') =>
  NextResponse.json(
    {
      ok: false,
      authenticated: false,
      error,
    },
    { status: 401 },
  )

const safeUpstreamPath = () => {
  try {
    const url = authSessionUrl()
    return `${url.origin}${url.pathname}`
  } catch {
    return '/auth/session'
  }
}

const logUpstreamAuthFailure = (status: number, reason: AuthFailureKind) => {
  console.warn('[auth/session] upstream auth check failed', {
    status,
    reason,
    upstream: safeUpstreamPath(),
  })
}

const logUpstreamUnavailable = (error: unknown) => {
  const reason = error instanceof Error && error.name === 'AbortError' ? 'timeout' : 'network_error'
  console.warn('[auth/session] upstream auth check unavailable', {
    reason,
    upstream: safeUpstreamPath(),
  })
}

export async function GET(req: NextRequest) {
  const cookie = req.headers.get('cookie')
  if (!hasRefreshCookie(cookie)) return authFailureResponse('unauthenticated')

  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS)

  try {
    const upstream = await fetch(authSessionUrl(), {
      method: 'GET',
      headers: {
        cookie: cookie ?? '',
        'x-forwarded-host':
          req.headers.get('x-forwarded-host') ??
          req.headers.get('host') ??
          '',
        'x-forwarded-proto':
          req.headers.get('x-forwarded-proto') ??
          req.nextUrl.protocol.replace(':', ''),
      },
      cache: 'no-store',
      signal: controller.signal,
    })

    const contentType = upstream.headers.get('content-type') ?? 'text/plain'
    const body = await upstream.text()

    if (!upstream.ok) {
      const reason = classifyAuthFailure(upstream.status, body.slice(0, 2048))
      logUpstreamAuthFailure(upstream.status, reason)

      return authFailureResponse(reason === 'unauthenticated' ? 'unauthenticated' : 'auth_upstream_unavailable')
    }

    return new NextResponse(body, {
      status: 200,
      headers: { 'content-type': contentType },
    })
  } catch (error) {
    logUpstreamUnavailable(error)
    return authFailureResponse('auth_upstream_unavailable')
  } finally {
    clearTimeout(timeout)
  }
}
