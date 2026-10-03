// Updates query-string view state (tabs, filters, an open sheet) without a server round trip: Next's router picks up
// history.replaceState for useSearchParams, so back/refresh/deep links keep working and nothing is refetched.
export const replaceQuery = (updates: Record<string, string | null | undefined>) => {
  const url = new URL(window.location.href)
  for (const [key, value] of Object.entries(updates)) {
    if (value === null || value === undefined || value === '') url.searchParams.delete(key)
    else url.searchParams.set(key, value)
  }
  window.history.replaceState(null, '', `${url.pathname}${url.search}${url.hash}`)
}
