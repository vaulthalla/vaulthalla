'use client'

import React, { useMemo, useState } from 'react'
import Link from 'next/link'
import { useRouter } from 'next/navigation'
import { cn } from '@/util/cn'
import { Input } from '@/components/ui/Field'

export interface Column<T> {
  key: string
  header: React.ReactNode
  cell: (row: T) => React.ReactNode
  // Enables header sorting on this column.
  sortValue?: (row: T) => string | number | null | undefined
  className?: string
  headerClassName?: string
  // Hide below this breakpoint.
  hideBelow?: 'sm' | 'md' | 'lg' | 'xl'
}

const hide = { sm: 'hidden sm:table-cell', md: 'hidden md:table-cell', lg: 'hidden lg:table-cell', xl: 'hidden xl:table-cell' }

// A compact, sortable, filterable list. Big lists (thousands of rows) belong in a virtualized view instead.
export function DataTable<T>({
  rows,
  columns,
  rowKey,
  onRowClick,
  rowHref,
  empty,
  filter,
  filterPlaceholder = 'Filter…',
  toolbar,
  className,
  initialSort,
  rowActions,
}: {
  rows: T[]
  columns: Column<T>[]
  rowKey: (row: T) => string | number
  onRowClick?: (row: T) => void
  rowHref?: (row: T) => string
  empty?: React.ReactNode
  filter?: (row: T, query: string) => boolean
  filterPlaceholder?: string
  toolbar?: React.ReactNode
  className?: string
  initialSort?: { key: string; dir: 'asc' | 'desc' }
  rowActions?: (row: T) => React.ReactNode
}) {
  const router = useRouter()
  const [query, setQuery] = useState('')
  const [sort, setSort] = useState(initialSort)

  const visible = useMemo(() => {
    const q = query.trim().toLowerCase()
    const filtered = q && filter ? rows.filter(row => filter(row, q)) : rows
    const column = sort ? columns.find(c => c.key === sort.key) : undefined
    if (!column?.sortValue || !sort) return filtered
    const dir = sort.dir === 'asc' ? 1 : -1
    return [...filtered].sort((a, b) => {
      const av = column.sortValue!(a)
      const bv = column.sortValue!(b)
      if (av === bv) return 0
      if (av === null || av === undefined) return 1
      if (bv === null || bv === undefined) return -1
      return (typeof av === 'number' && typeof bv === 'number' ? av - bv : String(av).localeCompare(String(bv))) * dir
    })
  }, [rows, query, filter, sort, columns])

  const toggleSort = (key: string) =>
    setSort(current => (current?.key === key ? { key, dir: current.dir === 'asc' ? 'desc' : 'asc' } : { key, dir: 'asc' }))

  const clickable = Boolean(onRowClick || rowHref)

  return (
    <div className={cn('panel overflow-hidden', className)}>
      {filter || toolbar ? (
        <div className="flex flex-wrap items-center gap-2 border-b border-line px-3 py-2.5">
          {filter ? (
            <Input
              value={query}
              onChange={event => setQuery(event.target.value)}
              placeholder={filterPlaceholder}
              aria-label={filterPlaceholder}
              className="h-8 max-w-xs bg-transparent"
            />
          ) : null}
          <div className="ml-auto flex flex-wrap items-center gap-2">{toolbar}</div>
        </div>
      ) : null}
      <div className="overflow-x-auto">
        <table className="w-full border-collapse text-sm">
          <thead>
            <tr className="border-b border-line text-left text-xs text-fg-subtle">
              {columns.map(column => (
                <th
                  key={column.key}
                  scope="col"
                  aria-sort={sort?.key === column.key ? (sort.dir === 'asc' ? 'ascending' : 'descending') : undefined}
                  className={cn('h-9 px-4 font-medium whitespace-nowrap', column.hideBelow && hide[column.hideBelow], column.headerClassName)}>
                  {column.sortValue ? (
                    <button type="button" onClick={() => toggleSort(column.key)} className="inline-flex items-center gap-1 hover:text-fg">
                      {column.header}
                      <span aria-hidden className={cn('text-[10px] opacity-0', sort?.key === column.key && 'opacity-100')}>
                        {sort?.dir === 'desc' ? '▼' : '▲'}
                      </span>
                    </button>
                  ) : (
                    column.header
                  )}
                </th>
              ))}
              {rowActions ? <th className="w-12" aria-label="Actions" /> : null}
            </tr>
          </thead>
          <tbody>
            {visible.map(row => {
              const href = rowHref?.(row)
              return (
                <tr
                  key={rowKey(row)}
                  onClick={
                    clickable
                      ? event => {
                          if ((event.target as HTMLElement).closest('button,a,input,[role="menuitem"]')) return
                          if (onRowClick) onRowClick(row)
                          else if (href) router.push(href)
                        }
                      : undefined
                  }
                  className={cn(
                    'group border-b border-line/60 last:border-0 transition-colors hover:bg-surface-2',
                    clickable && 'cursor-pointer',
                  )}>
                  {columns.map((column, index) => (
                    <td key={column.key} className={cn('h-11 px-4 align-middle', column.hideBelow && hide[column.hideBelow], column.className)}>
                      {index === 0 && href ? (
                        <Link href={href} className="font-medium text-fg hover:text-accent-text focus-visible:text-accent-text">
                          {column.cell(row)}
                        </Link>
                      ) : (
                        column.cell(row)
                      )}
                    </td>
                  ))}
                  {rowActions ? <td className="w-12 px-2 text-right">{rowActions(row)}</td> : null}
                </tr>
              )
            })}
          </tbody>
        </table>
      </div>
      {visible.length === 0 ? (
        <div className="px-6 py-10 text-center text-sm text-fg-subtle">{query ? `Nothing matches “${query}”.` : (empty ?? 'Nothing here yet.')}</div>
      ) : null}
    </div>
  )
}
