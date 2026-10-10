// Line diff (Myers, O((N+M)·D)) for the conflict preview. `a` is the local copy, `b` the remote one. Common prefix
// and suffix are trimmed first; past `maxEdits` differing lines the diff gives up (null) and the sheet shows the two
// texts without alignment instead of burning the main thread.

export type DiffKind = 'same' | 'local' | 'remote'

export interface DiffRow {
  kind: 'same' | 'change'
  left: { no: number; text: string } | null // local
  right: { no: number; text: string } | null // remote
}

type Op = { kind: DiffKind; text: string }

const myers = (A: string[], B: string[], maxEdits: number): Op[] | null => {
  const N = A.length
  const M = B.length
  const limit = Math.min(N + M, maxEdits)
  const off = limit + 1
  const v = new Int32Array(2 * off + 1)
  const trace: Int32Array[] = []
  let found = -1

  outer: for (let d = 0; d <= limit; d++) {
    trace.push(v.slice())
    for (let k = -d; k <= d; k += 2) {
      let x = k === -d || (k !== d && v[k - 1 + off] < v[k + 1 + off]) ? v[k + 1 + off] : v[k - 1 + off] + 1
      let y = x - k
      while (x < N && y < M && A[x] === B[y]) {
        x++
        y++
      }
      v[k + off] = x
      if (x >= N && y >= M) {
        found = d
        break outer
      }
    }
  }
  if (found < 0) return null

  const ops: Op[] = []
  let x = N
  let y = M
  for (let d = found; d >= 0; d--) {
    const t = trace[d]
    const k = x - y
    const prevK = k === -d || (k !== d && t[k - 1 + off] < t[k + 1 + off]) ? k + 1 : k - 1
    const prevX = t[prevK + off]
    const prevY = prevX - prevK
    while (x > prevX && y > prevY) {
      ops.push({ kind: 'same', text: A[x - 1] })
      x--
      y--
    }
    if (d > 0) {
      if (x === prevX) ops.push({ kind: 'remote', text: B[y - 1] })
      else ops.push({ kind: 'local', text: A[x - 1] })
    }
    x = prevX
    y = prevY
  }
  return ops.reverse()
}

export const splitLines = (text: string) => {
  const lines = text.split(/\r?\n/)
  if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop()
  return lines
}

// Aligned rows for a side-by-side view: unchanged lines pair up; inside a changed block, local-only and remote-only
// lines are zipped row by row.
export const diffRows = (local: string, remote: string, maxEdits = 1000): DiffRow[] | null => {
  const a = splitLines(local)
  const b = splitLines(remote)
  let prefix = 0
  while (prefix < a.length && prefix < b.length && a[prefix] === b[prefix]) prefix++
  let suffix = 0
  while (suffix < a.length - prefix && suffix < b.length - prefix && a[a.length - 1 - suffix] === b[b.length - 1 - suffix]) suffix++

  const middle = myers(a.slice(prefix, a.length - suffix), b.slice(prefix, b.length - suffix), maxEdits)
  if (!middle) return null

  const ops: Op[] = [
    ...a.slice(0, prefix).map(text => ({ kind: 'same' as const, text })),
    ...middle,
    ...a.slice(a.length - suffix).map(text => ({ kind: 'same' as const, text })),
  ]

  const rows: DiffRow[] = []
  let ln = 1
  let rn = 1
  let i = 0
  while (i < ops.length) {
    if (ops[i].kind === 'same') {
      rows.push({ kind: 'same', left: { no: ln++, text: ops[i].text }, right: { no: rn++, text: ops[i].text } })
      i++
      continue
    }
    const lefts: string[] = []
    const rights: string[] = []
    while (i < ops.length && ops[i].kind !== 'same') {
      if (ops[i].kind === 'local') lefts.push(ops[i].text)
      else rights.push(ops[i].text)
      i++
    }
    for (let j = 0; j < Math.max(lefts.length, rights.length); j++)
      rows.push({
        kind: 'change',
        left: j < lefts.length ? { no: ln++, text: lefts[j] } : null,
        right: j < rights.length ? { no: rn++, text: rights[j] } : null,
      })
  }
  return rows
}

// Long unchanged runs collapse to `context` lines around each change.
export type DiffItem = { type: 'row'; row: DiffRow } | { type: 'gap'; count: number }

export const collapse = (rows: DiffRow[], context = 3): DiffItem[] => {
  const keep = new Array<boolean>(rows.length).fill(false)
  rows.forEach((row, index) => {
    if (row.kind !== 'change') return
    for (let j = Math.max(0, index - context); j <= Math.min(rows.length - 1, index + context); j++) keep[j] = true
  })
  const items: DiffItem[] = []
  let gap = 0
  rows.forEach((row, index) => {
    if (keep[index]) {
      if (gap) items.push({ type: 'gap', count: gap })
      gap = 0
      items.push({ type: 'row', row })
    } else gap++
  })
  if (gap) items.push({ type: 'gap', count: gap })
  return items
}
