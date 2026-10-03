'use client'

import React, { useEffect, useMemo, useState } from 'react'
import Link from 'next/link'
import { api } from '@/lib/session'
import type { Payload } from '@/lib/ws/client'
import { useWs, invalidate } from '@/lib/query'
import { cn } from '@/util/cn'
import { PageHeader, Panel } from '@/components/ui/Panel'
import { Button } from '@/components/ui/Button'
import { Badge } from '@/components/ui/Badge'
import { EmptyState, InlineError, QueryState } from '@/components/ui/State'
import { notify } from '@/components/ui/Toast'
import { ArrowUpRightFromSquareIcon, TriangleExclamationIcon } from '@/components/ui/icons'
import { titleCase } from '@/lib/format'
import { useIsSuperAdmin } from '@/features/cost/queries'
import { SECTIONS, type FieldDef, type SectionDef } from '@/features/settings/schema'
import { SettingField, genericDef, validate } from '@/features/settings/fields'
import { getAt, isObject, leafPaths, mergePatch, setAt, type Json, type JsonObject } from '@/features/settings/patch'

interface Block {
  path: string[]
  label?: string
  fields: FieldDef[]
}

// The schema's fields for a section plus a generic block for keys the schema doesn't know.
const blocksFor = (section: SectionDef, value: Json | undefined): Block[] => {
  const blocks: Block[] = []
  if (section.fields.length) blocks.push({ path: [section.key], fields: section.fields })
  for (const group of section.groups ?? [])
    blocks.push({ path: [section.key, ...group.key.split('.')], label: group.label, fields: group.fields })
  if (isObject(value)) {
    const known = new Set([...section.fields.map(f => f.key), ...(section.groups ?? []).map(g => g.key.split('.')[0])])
    const extra = Object.entries(value)
      .filter(([key]) => !known.has(key))
      .map(([key, v]) => genericDef(key, v))
      .filter((d): d is FieldDef => d !== null)
    if (extra.length) blocks.push({ path: [section.key], label: 'Other', fields: extra })
  }
  return blocks
}

const pathKey = (path: string[]) => path.join('.')

const Editor = ({ settings }: { settings: JsonObject }) => {
  const [draft, setDraft] = useState<JsonObject>(settings)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<unknown>(null)

  const sections = useMemo(() => {
    const known = new Set(SECTIONS.map(s => s.key))
    const unknown: SectionDef[] = Object.keys(settings)
      .filter(key => !known.has(key) && isObject(settings[key]))
      .map(key => ({
        key,
        label: titleCase(key),
        description: 'A setting this console doesn’t describe yet.',
        fields: [],
      }))
    return [...SECTIONS.filter(s => s.key in settings), ...unknown]
  }, [settings])

  const editable = sections.filter(s => !s.managedAt)
  const patch = useMemo(() => {
    const before: JsonObject = {}
    const after: JsonObject = {}
    for (const s of editable) {
      before[s.key] = settings[s.key]
      after[s.key] = draft[s.key]
    }
    return mergePatch(before, after)
  }, [draft, settings, editable])
  const changed = useMemo(() => new Set(leafPaths(patch).map(pathKey)), [patch])

  const errors = useMemo(() => {
    const list: string[] = []
    for (const s of editable)
      for (const b of blocksFor(s, settings[s.key]))
        for (const f of b.fields)
          if (validate(f.type, getAt(draft, [...b.path, f.key])) && changed.has(pathKey([...b.path, f.key])))
            list.push(f.label)
    return list
  }, [draft, settings, editable, changed])

  const restartChanges = [...changed].filter(p => {
    const [sectionKey] = p.split('.')
    const section = SECTIONS.find(s => s.key === sectionKey)
    if (section?.restart) return true
    return Boolean(
      section
      && [...section.fields, ...(section.groups ?? []).flatMap(g => g.fields)].find(
        f => f.restart && p === `${sectionKey}.${f.key}`,
      ),
    )
  })

  const dirty = changed.size > 0
  useEffect(() => {
    if (!dirty) return
    const guard = (event: BeforeUnloadEvent) => event.preventDefault()
    window.addEventListener('beforeunload', guard)
    return () => window.removeEventListener('beforeunload', guard)
  }, [dirty])

  const save = async () => {
    setBusy(true)
    setError(null)
    try {
      await api.send('settings.update', patch as Payload<'settings.update'>)
      await invalidate('settings.get', 'email.config.get', 's3.gateway.status')
      notify.success('Settings saved', '/etc/vaulthalla/config.yaml was updated.')
      if (restartChanges.length)
        notify.info('Restart to apply', 'Some changes take effect after `sudo systemctl restart vaulthalla`.')
    } catch (err) {
      setError(err)
    } finally {
      setBusy(false)
    }
  }

  const sectionChanges = (key: string) => [...changed].filter(p => p.startsWith(`${key}.`)).length

  return (
    <div className="grid gap-8 lg:grid-cols-[13rem_minmax(0,1fr)]">
      <nav aria-label="Settings sections" className="hidden lg:block">
        <ul className="sticky top-4 space-y-0.5 text-sm">
          {sections.map(s => (
            <li key={s.key}>
              <a
                href={`#${s.key}`}
                className="text-fg-subtle hover:bg-surface-2 hover:text-fg flex items-center justify-between gap-2 rounded-md px-2.5 py-1.5 transition-colors">
                <span className="truncate">{s.label}</span>
                {sectionChanges(s.key) ?
                  <span className="bg-accent size-1.5 shrink-0 rounded-full" aria-label="Has changes" />
                : null}
              </a>
            </li>
          ))}
        </ul>
      </nav>

      <div className="min-w-0 space-y-5">
        {sections.map(section => (
          <Panel
            key={section.key}
            id={section.key}
            className="scroll-mt-4"
            title={
              <span className="flex flex-wrap items-center gap-2">
                {section.label}
                {section.restart ?
                  <Badge tone="neutral" className="h-5 px-2 text-[11px]">
                    restart required
                  </Badge>
                : null}
              </span>
            }
            description={
              <>
                {section.description}
                {section.applies ?
                  <span className="text-fg-faint mt-0.5 block text-xs">{section.applies}</span>
                : null}
              </>
            }>
            {section.managedAt ?
              <Button asChild variant="secondary" size="sm">
                <Link href={section.managedAt.href}>
                  Manage in {section.managedAt.label}
                  <ArrowUpRightFromSquareIcon aria-hidden />
                </Link>
              </Button>
            : <div className="space-y-5">
                {section.warning ?
                  <p className="rounded-control border-warn-line bg-warn-soft text-warn flex items-start gap-2 border px-3 py-2 text-sm">
                    <TriangleExclamationIcon className="mt-0.5 size-4 shrink-0" aria-hidden />
                    {section.warning}
                  </p>
                : null}
                {blocksFor(section, settings[section.key]).map(block => (
                  <div
                    key={`${pathKey(block.path)}:${block.label ?? ''}`}
                    className={cn(block.label && 'border-line border-t pt-4')}>
                    {block.label ?
                      <h3 className="text-fg-muted mb-3 text-[13px] font-semibold tracking-wide uppercase">
                        {block.label}
                      </h3>
                    : null}
                    <div className="grid gap-4 sm:grid-cols-2">
                      {block.fields.map(f => {
                        const path = [...block.path, f.key]
                        return (
                          <SettingField
                            key={pathKey(path)}
                            id={`setting-${pathKey(path)}`}
                            def={f}
                            value={getAt(draft, path)}
                            changed={changed.has(pathKey(path))}
                            onChange={v => setDraft(d => setAt(d, path, v))}
                          />
                        )
                      })}
                    </div>
                  </div>
                ))}
              </div>
            }
          </Panel>
        ))}
        {dirty ?
          <div className="glass-strong rounded-panel sticky bottom-3 z-30 flex flex-wrap items-center gap-3 px-4 py-3">
            <div className="min-w-0 flex-1 text-sm">
              <span className="text-fg font-medium">
                {changed.size} unsaved change{changed.size === 1 ? '' : 's'}
              </span>
              {restartChanges.length ?
                <span className="text-fg-subtle">
                  {' '}
                  · {restartChanges.length} need{restartChanges.length === 1 ? 's' : ''} a daemon restart
                </span>
              : null}
              {errors.length ?
                <span className="text-danger block text-xs">Fix: {errors.join(', ')}</span>
              : null}
              {error ?
                <InlineError error={error} className="mt-2" />
              : null}
            </div>
            <Button variant="ghost" onClick={() => setDraft(settings)} disabled={busy}>
              Discard
            </Button>
            <Button variant="primary" onClick={() => void save()} loading={busy} disabled={errors.length > 0}>
              Save changes
            </Button>
          </div>
        : null}
      </div>
    </div>
  )
}

export const SettingsPage = () => {
  const allowed = useIsSuperAdmin()
  const query = useWs('settings.get', null, {
    enabled: allowed,
    staleTime: Infinity,
    select: data => data.settings as unknown as JsonObject,
  })
  return (
    <>
      <PageHeader
        title="Settings"
        description={
          <>
            The daemon’s configuration in{' '}
            <code className="text-fg-muted font-mono text-xs">/etc/vaulthalla/config.yaml</code>. Only the values you
            change are written.
          </>
        }
      />
      {!allowed ?
        <EmptyState
          className="panel"
          title="You don't have access to this"
          description="Server settings are managed by super admins."
        />
      : <QueryState query={query}>
          {settings => <Editor key={JSON.stringify(settings)} settings={settings} />}
        </QueryState>
      }
    </>
  )
}
