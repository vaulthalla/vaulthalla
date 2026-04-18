import { NavItem, NavConfig } from '@/components/nav/types'
import FolderPlus from '@/fa-duotone/folder-plus.svg'

export const fsNavItems: NavItem[] = [{ label: 'Add Directory', href: '/files/add-directory', icon: FolderPlus }]

export const fsNav: NavConfig = { items: fsNavItems }
