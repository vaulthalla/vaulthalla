import { NavItem, NavConfig } from '@/components/nav/types'
import Vault from '@/fa-duotone/vault.svg'
import KeySkeleton from '@/fa-duotone/key-skeleton.svg'
import UserGroup from '@/fa-duotone/user-group.svg'
import PeopleGroup from '@/fa-duotone/people-group.svg'
import Sliders from '@/fa-duotone/sliders.svg'
import ShieldKeyhole from '@/fa-duotone/shield-keyhole.svg'
import UserShield from '@/fa-duotone/user-shield.svg'
import VaultShield from '@/fa-duotone/file-shield.svg'

export const adminNavItems: NavItem[] = [
  { label: 'Dashboard', href: '/dashboard', icon: Vault },
  { label: 'Vaults', href: '/vaults', icon: Vault },
  { label: 'API Keys', href: '/api-keys', icon: KeySkeleton },
  { label: 'Users', href: '/users', icon: UserGroup },
  {
    label: 'Roles',
    href: '/roles',
    icon: ShieldKeyhole,
    subItems: [
      { label: 'Admin', href: '/roles/admin', icon: UserShield },
      { label: 'Vault', href: '/roles/vault', icon: VaultShield },
    ],
  },
  { label: 'Groups', href: '/groups', icon: PeopleGroup },
  { label: 'Settings', href: '/settings', icon: Sliders },
]

export const adminNav: NavConfig = { items: adminNavItems }
