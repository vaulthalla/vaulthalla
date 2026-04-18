import React from 'react'
import { AdminPage } from '@/components/admin/AdminPage'
import { AdminGrid } from '@/components/admin/AdminGrid'
import { AddButton } from '@/components/admin/AddButton'

type AdminPageOpts = {
  title: string
  description: string
  add?: { title: string; href: string }
  grid?: boolean
  actions?: React.ReactNode
}

export const makeAdminPage = (ClientContent: React.ComponentType, opts: AdminPageOpts) => {
  return () => {
    const { title, description, add, grid = false, actions } = opts

    const Body =
      grid ?
        <AdminGrid>
          <ClientContent />
        </AdminGrid>
      : <ClientContent />

    return (
      <AdminPage title={title} description={description}>
        <div className="flex items-center justify-between gap-3">
          <div className="flex items-center gap-3">{add && <AddButton title={add.title} href={add.href} />}</div>
          {actions}
        </div>

        {Body}
      </AdminPage>
    )
  }
}
