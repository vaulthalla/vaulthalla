// Server-side session check against the daemon's HTTP /auth/session (refresh cookie). Shared by the middleware
// and /api/auth/session so a page request costs one upstream hop, not two.

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

export type SessionCheck = { kind: 'ok'; body: string; contentType: string } | { kind: 'unauthenticated' } | { kind: 'unavailable' }

export const resolveAuthOrigin = () =>
  (process.env.VAULTHALLA_AUTH_ORIGIN ?? process.env.VAULTHALLA_PREVIEW_ORIGIN ?? AUTH_ORIGIN_FALLBACK).replace(/\/+$/, '')

const authSessionUrl = () => new URL('/auth/session', resolveAuthOrigin())

const upstreamLabel = () => {
  try {
    const url = authSessionUrl()
    return `${url.origin}${url.pathname}`
  } catch {
    return '/auth/session'
  }
}

export const hasRefreshCookie = (cookieHeader: string | null) =>
  Boolean(cookieHeader?.split(';').some(cookie => cookie.trim().startsWith('refresh=')))

export async function checkSession(cookie: string | null, forwardedHost: string, forwardedProto: string): Promise<SessionCheck> {
  if (!hasRefreshCookie(cookie)) return { kind: 'unauthenticated' }

  const controller = new AbortController()
  const timeout = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS)
  try {
    const upstream = await fetch(authSessionUrl(), {
      method: 'GET',
      headers: { cookie: cookie ?? '', 'x-forwarded-host': forwardedHost, 'x-forwarded-proto': forwardedProto },
      cache: 'no-store',
      signal: controller.signal,
    })
    const contentType = upstream.headers.get('content-type') ?? 'text/plain'
    const body = await upstream.text()
    if (upstream.ok) return { kind: 'ok', body, contentType }

    const normalized = body.slice(0, 2048).toLowerCase()
    const unauthenticated =
      upstream.status === 401 || upstream.status === 403 || UNAUTHENTICATED_BODY_MARKERS.some(marker => normalized.includes(marker))
    console.warn('[auth/session] upstream auth check failed', {
      status: upstream.status,
      reason: unauthenticated ? 'unauthenticated' : 'upstream_error',
      upstream: upstreamLabel(),
    })
    return unauthenticated ? { kind: 'unauthenticated' } : { kind: 'unavailable' }
  } catch (error) {
    console.warn('[auth/session] upstream auth check unavailable', {
      reason: error instanceof Error && error.name === 'AbortError' ? 'timeout' : 'network_error',
      upstream: upstreamLabel(),
    })
    return { kind: 'unavailable' }
  } finally {
    clearTimeout(timeout)
  }
}
