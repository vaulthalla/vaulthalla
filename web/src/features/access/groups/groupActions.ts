import { confirm } from '@/components/ui/Confirm'
import { notify } from '@/components/ui/Toast'
import type { GroupRecord } from '@/features/access/types'

export const GROUP_COMMANDS = ['groups.list', 'group.get', 'groups.list.byUser'] as const

export const deleteGroup = async (group: GroupRecord, remove: (id: number) => Promise<unknown>) => {
  const ok = await confirm({
    title: `Delete group ${group.name}?`,
    description: `${group.members.length ? `Its ${group.members.length} member${group.members.length === 1 ? '' : 's'} lose` : 'Members lose'} every vault role granted through this group. The accounts themselves stay.`,
    confirmLabel: 'Delete group',
  })
  if (!ok) return false
  try {
    await remove(group.id)
    notify.success(`Deleted group ${group.name}`)
    return true
  } catch (error) {
    notify.error(error, 'Could not delete the group')
    return false
  }
}
