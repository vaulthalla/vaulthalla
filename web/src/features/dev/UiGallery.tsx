'use client'

import React, { useState } from 'react'
import { Button } from '@/components/ui/Button'
import { IconButton } from '@/components/ui/IconButton'
import { Field, Input, Select, Textarea } from '@/components/ui/Field'
import { Checkbox, Switch, SwitchRow } from '@/components/ui/Choice'
import { Badge, Dot, Kbd, SeverityBadge } from '@/components/ui/Badge'
import { DefinitionList, PageHeader, Panel } from '@/components/ui/Panel'
import { EmptyState, ErrorState, InlineError, Skeleton } from '@/components/ui/State'
import { Meter, SegmentBar, Sparkline, StatGrid, StatTile } from '@/components/ui/Stat'
import { DataTable } from '@/components/ui/DataTable'
import { DropdownMenu } from '@/components/ui/Menu'
import { Tooltip } from '@/components/ui/Tooltip'
import { Segmented, Tabs, TabsContent, TabsList, TabsTrigger } from '@/components/ui/Tabs'
import { Dialog, DialogContent, DialogTrigger } from '@/components/ui/Dialog'
import { Spinner } from '@/components/ui/Spinner'
import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import { WsError } from '@/lib/ws/errors'
import { DownloadIcon, FolderIcon, PenIcon, PlusIcon, TrashIcon, EllipsisIcon } from '@/components/ui/icons'

const rows = [
  { id: 1, name: 'Admin Default Vault', type: 'local', used: 0.42 },
  { id: 2, name: 'R2 Test Vault', type: 's3', used: null },
  { id: 3, name: 'Archive', type: 's3', used: 0.91 },
]

export function UiGallery() {
  const [on, setOn] = useState(true)
  const [checked, setChecked] = useState<boolean | 'indeterminate'>('indeterminate')
  const [seg, setSeg] = useState<'admin' | 'vault'>('admin')
  return (
    <div className="space-y-6">
      <PageHeader eyebrow="Development" title="UI gallery" description="Every primitive in every state. Visual regression target; not shipped in production builds." actions={<Button variant="primary"><PlusIcon aria-hidden /> Primary</Button>} />

      <Panel title="Buttons" description="One solid cyan action per view.">
        <div className="flex flex-wrap items-center gap-2">
          <Button variant="primary">Primary</Button>
          <Button>Secondary</Button>
          <Button variant="ghost">Ghost</Button>
          <Button variant="subtle">Subtle</Button>
          <Button variant="danger">Danger</Button>
          <Button variant="danger-solid">Danger solid</Button>
          <Button variant="link">Link</Button>
          <Button loading>Loading</Button>
          <Button disabled>Disabled</Button>
          <Button size="sm">Small</Button>
          <Button size="lg" variant="primary">Large</Button>
          <IconButton label="Rename" icon={PenIcon} />
          <IconButton label="Delete" icon={TrashIcon} variant="danger" />
        </div>
      </Panel>

      <div className="grid gap-6 lg:grid-cols-2">
        <Panel title="Fields">
          <div className="grid gap-4">
            <Field label="Name" htmlFor="g-name" hint="Shown to everyone" required>
              <Input id="g-name" placeholder="Vault name" />
            </Field>
            <Field label="With error" htmlFor="g-err" error="That name is taken">
              <Input id="g-err" aria-invalid="true" defaultValue="Admin Default Vault" />
            </Field>
            <Field label="Provider" htmlFor="g-sel">
              <Select id="g-sel" defaultValue="r2">
                <option value="aws">AWS</option>
                <option value="r2">Cloudflare R2</option>
              </Select>
            </Field>
            <Field label="Description" htmlFor="g-ta">
              <Textarea id="g-ta" placeholder="Optional" />
            </Field>
            <div className="flex items-center gap-3">
              <Checkbox aria-label="Indeterminate" checked={checked} onCheckedChange={setChecked} />
              <Switch aria-label="Enabled" checked={on} onCheckedChange={setOn} />
              <Segmented label="Type" value={seg} onChange={setSeg} options={[{ value: 'admin', label: 'Admin' }, { value: 'vault', label: 'Vault' }]} />
            </div>
            <SwitchRow id="g-sw" label="Encrypt upstream" hint="Objects are encrypted before they leave this host" checked={on} onCheckedChange={setOn} />
            <InlineError error={new Error('The server refused the request')} />
          </div>
        </Panel>

        <Panel title="Status" description="Tones come only from backend severity.">
          <div className="flex flex-wrap gap-2">
            <SeverityBadge severity="healthy" />
            <SeverityBadge severity="info" />
            <SeverityBadge severity="warning" />
            <SeverityBadge severity="error" />
            <SeverityBadge severity={null} />
            <Badge tone="accent" dot pulse>live</Badge>
            <Badge>neutral</Badge>
            <span className="inline-flex items-center gap-2 text-sm text-fg-muted"><Dot tone="ok" /> ok <Dot tone="warn" /> warn <Dot tone="danger" /> danger</span>
            <Kbd>⌘</Kbd><Kbd>K</Kbd>
          </div>
          <StatGrid className="mt-4">
            <StatTile label="Used" value="3.07 KB" hint="0.0% of 10 GB" />
            <StatTile label="Pending" value="0" />
            <StatTile label="Failed 24h" value="2" tone="danger" />
            <StatTile label="Hit ratio" value={null} />
          </StatGrid>
          <div className="mt-4 space-y-3">
            <Meter ratio={0.42} label="usage" />
            <Meter ratio={0.91} tone="warn" label="usage" />
            <Meter ratio={null} label="unknown" />
            <SegmentBar data={[{ label: 'Data', value: 70 }, { label: 'Cache', value: 20, tone: 'info' }, { label: 'Free', value: 40, tone: 'neutral' }]} />
            <Sparkline values={[3, 5, 4, 8, 6, 9, 7, 12, 10, 14]} />
          </div>
        </Panel>
      </div>

      <DataTable
        rows={rows}
        rowKey={row => row.id}
        filter={(row, q) => row.name.toLowerCase().includes(q)}
        toolbar={<Button size="sm" variant="primary"><PlusIcon aria-hidden /> New</Button>}
        columns={[
          { key: 'name', header: 'Name', cell: row => <span className="inline-flex items-center gap-2"><FolderIcon className="size-4 text-accent-text" aria-hidden />{row.name}</span>, sortValue: row => row.name },
          { key: 'type', header: 'Type', cell: row => <Badge>{row.type}</Badge>, sortValue: row => row.type },
          { key: 'used', header: 'Usage', cell: row => <Meter ratio={row.used} className="w-32" />, hideBelow: 'md' },
        ]}
        rowActions={() => (
          <DropdownMenu
            label="Row actions"
            entries={[{ key: 'dl', label: 'Download', icon: DownloadIcon, onSelect: () => notify.success('Download started') }, 'separator', { key: 'rm', label: 'Delete', icon: TrashIcon, danger: true, onSelect: () => void confirm({ title: 'Delete Archive?', description: 'This cannot be undone.', confirmLabel: 'Delete' }) }]}
            trigger={<button type="button" aria-label="Row actions" className="grid size-8 place-items-center rounded-md text-fg-subtle hover:bg-surface-3"><EllipsisIcon className="size-4" aria-hidden /></button>}
          />
        )}
      />

      <div className="grid gap-6 lg:grid-cols-3">
        <Panel title="Overlays">
          <div className="flex flex-wrap gap-2">
            <Dialog>
              <DialogTrigger asChild><Button>Dialog</Button></DialogTrigger>
              <DialogContent title="Rename vault" description="Names are shown in the FUSE mount." footer={<><Button variant="ghost">Cancel</Button><Button variant="primary">Save</Button></>}>
                <Field label="Name" htmlFor="g-dlg"><Input id="g-dlg" defaultValue="Archive" /></Field>
              </DialogContent>
            </Dialog>
            <Button onClick={() => void confirm({ title: 'Delete vault “Archive”?', description: 'Type the name to confirm.', typeToConfirm: 'Archive', confirmLabel: 'Delete vault' })}>Typed confirm</Button>
            <Button onClick={() => notify.success('Saved', 'Settings applied')}>Toast</Button>
            <Button onClick={() => notify.error(new Error('Upload failed: quota exceeded'))}>Error toast</Button>
            <Tooltip content="Tooltip text"><Button variant="ghost">Hover me</Button></Tooltip>
          </div>
        </Panel>
        <Panel title="Tabs">
          <Tabs defaultValue="a">
            <TabsList><TabsTrigger value="a">Overview</TabsTrigger><TabsTrigger value="b">Access</TabsTrigger></TabsList>
            <TabsContent value="a"><DefinitionList items={[['Owner', 'admin'], ['Type', 'S3'], ['Created', 'Oct 3, 2026']]} /></TabsContent>
            <TabsContent value="b"><p className="text-sm text-fg-subtle">Assignments…</p></TabsContent>
          </Tabs>
        </Panel>
        <Panel title="Loading">
          <div className="space-y-2"><Skeleton /><Skeleton className="w-2/3" /><Spinner /></div>
        </Panel>
      </div>

      <div className="grid gap-6 lg:grid-cols-3">
        <Panel padded={false}><EmptyState icon={FolderIcon} title="This folder is empty" description="Drop files anywhere on this page." action={<Button>Upload</Button>} /></Panel>
        <Panel padded={false}><ErrorState error={new WsError('denied', 'Must be an admin')} /></Panel>
        <Panel padded={false}><ErrorState error={new WsError('disconnected', 'Cannot reach the Vaulthalla server')} onRetry={() => undefined} /></Panel>
      </div>
    </div>
  )
}
