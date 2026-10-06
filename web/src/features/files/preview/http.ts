// HTTP helpers shared by the preview renderers. Every request is same-origin with the session cookie (console
// `refresh`, or `share_refresh` on the `share=1` lane); no tokens in URLs.

export class PreviewHttpError extends Error {
  constructor(
    public status: number,
    public code: string,
    message: string,
  ) {
    super(message)
  }
}

export const describeStatus = (status: number, what = 'this file') => {
  if (status === 401) return 'Your session has ended. Sign in again to view this file.'
  if (status === 403) return `You don’t have permission to view ${what}.`
  if (status === 404) return 'This file no longer exists.'
  if (status === 413) return `${what[0].toUpperCase()}${what.slice(1)} is too large to show here.`
  if (status === 415) return `${what[0].toUpperCase()}${what.slice(1)} can’t be shown in the browser.`
  if (status === 429 || status === 503) return 'The server is busy. Try again in a moment.'
  if (status === 0) return 'The server could not be reached.'
  return `The server could not load ${what} (HTTP ${status}).`
}

export const isAbort = (error: unknown) => error instanceof DOMException && error.name === 'AbortError'

export const errorText = (error: unknown) => (error instanceof Error ? error.message : String(error))

const sleep = (ms: number, signal: AbortSignal) =>
  new Promise<void>((resolve, reject) => {
    if (signal.aborted) return reject(new DOMException('Cancelled', 'AbortError'))
    const timer = setTimeout(() => {
      signal.removeEventListener('abort', onAbort)
      resolve()
    }, ms)
    const onAbort = () => {
      clearTimeout(timer)
      reject(new DOMException('Cancelled', 'AbortError'))
    }
    signal.addEventListener('abort', onAbort, { once: true })
  })

const readJson = async (response: Response): Promise<Record<string, unknown> | null> => {
  try {
    return (await response.json()) as Record<string, unknown>
  } catch {
    return null
  }
}

const POLL_LIMIT_MS = 15 * 60_000

// Waits for a server-derived artifact (`/preview/derived`): 202 means queued (poll with backoff, honouring
// Retry-After), 200/206 means ready. `probe` asks for one byte only, so readiness never downloads the artifact; the
// caller cancels the body. Typed failures: 503 converter_unavailable (optional helper package not installed),
// 422 failed (with the converter's reason), 415 unsupported.
export async function pollDerived(
  url: string,
  options: { signal: AbortSignal; probe?: boolean; onQueued?: (elapsedMs: number) => void },
): Promise<Response> {
  const started = Date.now()
  let delay = 1000
  for (;;) {
    const response = await fetch(url, {
      credentials: 'same-origin',
      cache: 'no-store',
      signal: options.signal,
      headers: options.probe ? { Range: 'bytes=0-0' } : undefined,
    })
    if (response.status === 200 || response.status === 206) return response
    if (response.status === 202) {
      void response.body?.cancel().catch(() => undefined)
      const elapsed = Date.now() - started
      if (elapsed > POLL_LIMIT_MS) throw new PreviewHttpError(504, 'timeout', 'The conversion is taking too long. Try again later.')
      options.onQueued?.(elapsed)
      const retryAfter = Number(response.headers.get('retry-after'))
      await sleep(Math.max(Number.isFinite(retryAfter) ? retryAfter * 1000 : 0, delay), options.signal)
      delay = Math.min(10_000, Math.round(delay * 1.5))
      continue
    }
    const body = await readJson(response)
    if (response.status === 503 && body?.code === 'converter_unavailable') {
      const helper = typeof body.helper === 'string' ? body.helper : 'conversion'
      throw new PreviewHttpError(503, 'converter_unavailable', `The optional ${helper} package isn’t installed on this server, so it can’t convert this file.`)
    }
    if (response.status === 422) {
      const reason = typeof body?.reason === 'string' && body.reason ? body.reason : null
      throw new PreviewHttpError(422, 'failed', reason ? `The conversion failed: ${reason}` : 'The conversion failed.')
    }
    if (response.status === 415) throw new PreviewHttpError(415, 'unsupported', 'The server can’t convert this file.')
    throw new PreviewHttpError(response.status, 'http', describeStatus(response.status))
  }
}

// Reads a response body into one ArrayBuffer, refusing anything over `maxBytes` (by Content-Length up front, and
// while streaming when the length is unknown). Reports progress per chunk.
export async function readBytes(
  response: Response,
  maxBytes: number,
  onProgress?: (loaded: number, total: number | null) => void,
): Promise<ArrayBuffer> {
  const declared = Number(response.headers.get('content-length') ?? '')
  const total = Number.isFinite(declared) && declared > 0 ? declared : null
  const tooLarge = () => new PreviewHttpError(413, 'too_large', 'This file is too large to open in the browser.')
  if (total !== null && total > maxBytes) {
    void response.body?.cancel().catch(() => undefined)
    throw tooLarge()
  }
  if (!response.body) {
    const buffer = await response.arrayBuffer()
    if (buffer.byteLength > maxBytes) throw tooLarge()
    onProgress?.(buffer.byteLength, buffer.byteLength)
    return buffer
  }
  const reader = response.body.getReader()
  let loaded = 0
  // One allocation when the length is known; otherwise collect and join.
  const exact = total !== null ? new Uint8Array(total) : null
  const chunks: Uint8Array[] = []
  for (;;) {
    const { done, value } = await reader.read()
    if (done) break
    if (loaded + value.byteLength > maxBytes || (exact && loaded + value.byteLength > exact.byteLength)) {
      void reader.cancel().catch(() => undefined)
      throw exact && loaded + value.byteLength <= maxBytes ? new PreviewHttpError(502, 'length', 'The server sent more data than announced.') : tooLarge()
    }
    if (exact) exact.set(value, loaded)
    else chunks.push(value)
    loaded += value.byteLength
    onProgress?.(loaded, total)
  }
  if (exact) return loaded === exact.byteLength ? exact.buffer : exact.slice(0, loaded).buffer
  const joined = new Uint8Array(loaded)
  let offset = 0
  for (const chunk of chunks) {
    joined.set(chunk, offset)
    offset += chunk.byteLength
  }
  return joined.buffer
}
