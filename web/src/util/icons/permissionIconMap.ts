import type React from 'react'

import UsersGear from '@/fa-duotone/users-gear.svg'
import UserShield from '@/fa-duotone/user-shield.svg'
import Sliders from '@/fa-light/sliders.svg'

import ArrowRightArrowLeft from '@/fa-regular/arrow-right-arrow-left.svg'
import ArrowUpFromBracket from '@/fa-regular/arrow-up-from-bracket.svg'
import Bolt from '@/fa-regular/bolt.svg'
import BoxesStacked from '@/fa-regular/boxes-stacked.svg'
import ChartSimple from '@/fa-regular/chart-simple.svg'
import Cloud from '@/fa-regular/cloud.svg'
import CloudArrowDown from '@/fa-regular/cloud-arrow-down.svg'
import CloudArrowUp from '@/fa-regular/cloud-arrow-up.svg'
import ClipboardList from '@/fa-regular/clipboard-list.svg'
import ClockRotateLeft from '@/fa-regular/clock-rotate-left.svg'
import Compress from '@/fa-regular/compress.svg'
import Copy from '@/fa-regular/copy.svg'
import Database from '@/fa-regular/database.svg'
import DiagramProject from '@/fa-regular/diagram-project.svg'
import Eye from '@/fa-regular/eye.svg'
import FileArrowDown from '@/fa-regular/file-arrow-down.svg'
import FileArrowUp from '@/fa-regular/file-arrow-up.svg'
import FileLock from '@/fa-regular/file-lock.svg'
import Fingerprint from '@/fa-regular/fingerprint.svg'
import FolderOpen from '@/fa-regular/folder-open.svg'
import FolderPlus from '@/fa-regular/folder-plus.svg'
import Globe from '@/fa-regular/globe.svg'
import Gears from '@/fa-regular/gears.svg'
import HardDrive from '@/fa-regular/hard-drive.svg'
import Key from '@/fa-regular/key.svg'
import KeySkeleton from '@/fa-regular/key-skeleton.svg'
import Keyhole from '@/fa-regular/lock-keyhole.svg'
import ListUl from '@/fa-regular/list-ul.svg'
import Lock from '@/fa-regular/lock.svg'
import ObjectGroup from '@/fa-regular/object-group.svg'
import PenToSquare from '@/fa-regular/pen-to-square.svg'
import Plus from '@/fa-regular/plus.svg'
import Server from '@/fa-regular/server.svg'
import ShareFromSquare from '@/fa-regular/share-from-square.svg'
import ShareNodes from '@/fa-regular/share-nodes.svg'
import ShieldHalved from '@/fa-regular/shield-halved.svg'
import ShieldKeyhole from '@/fa-regular/shield-keyhole.svg'
import SquareShareNodes from '@/fa-regular/square-share-nodes.svg'
import TrashCan from '@/fa-regular/trash-can.svg'
import Unlock from '@/fa-regular/unlock.svg'
import UserGear from '@/fa-regular/user-gear.svg'
import UserGroup from '@/fa-regular/user-group.svg'
import UserLock from '@/fa-regular/user-lock.svg'
import UserPen from '@/fa-regular/user-pen.svg'
import UserPlus from '@/fa-regular/user-plus.svg'
import Users from '@/fa-regular/users.svg'
import UsersBetweenLines from '@/fa-regular/users-between-lines.svg'
import UserXmark from '@/fa-regular/user-xmark.svg'

export const permissionIconMap: Record<string, React.FC<React.SVGProps<SVGSVGElement>>> = {
  // Generic action fallbacks
  view: Eye,
  read: Eye,
  list: ListUl,

  add: Plus,
  create: Plus,

  edit: PenToSquare,
  update: PenToSquare,
  rename: PenToSquare,

  delete: TrashCan,
  remove: TrashCan,

  move: ArrowRightArrowLeft,
  copy: Copy,

  upload: CloudArrowUp,
  download: CloudArrowDown,

  share: ShareFromSquare,

  lock: Lock,
  unlock: Unlock,
  restore: ClockRotateLeft,

  export: ArrowUpFromBracket,
  rotate: Gears,
  consume: Bolt,

  view_stats: ChartSimple,

  'add-member': UserPlus,
  'remove-member': UserXmark,
  'view-members': UsersBetweenLines,
  add_member: UserPlus,
  remove_member: UserXmark,
  view_members: UsersBetweenLines,

  // Vault
  'vault.sync': Cloud,
  'vault.sync.upload': CloudArrowUp,
  'vault.sync.download': CloudArrowDown,
  'vault.sync.full': Gears,
  'vault.sync.status': ChartSimple,

  'vault.roles.view': UserLock,
  'vault.roles.assign': UserShield,
  'vault.roles.edit': UserGear,
  'vault.roles.delete': UserXmark,

  'vault.fs.files.view': Eye,
  'vault.fs.files.read': Eye,
  'vault.fs.files.create': FileArrowUp,
  'vault.fs.files.upload': FileArrowUp,
  'vault.fs.files.download': FileArrowDown,
  'vault.fs.files.edit': PenToSquare,
  'vault.fs.files.write': PenToSquare,
  'vault.fs.files.rename': PenToSquare,
  'vault.fs.files.move': ArrowRightArrowLeft,
  'vault.fs.files.copy': Copy,
  'vault.fs.files.delete': TrashCan,
  'vault.fs.files.remove': TrashCan,
  'vault.fs.files.lock': FileLock,
  'vault.fs.files.unlock': Unlock,
  'vault.fs.files.share': ShareFromSquare,
  'vault.fs.files.share.internal': ShareNodes,
  'vault.fs.files.share.public': Globe,

  'vault.fs.directories.view': FolderOpen,
  'vault.fs.directories.list': FolderOpen,
  'vault.fs.directories.create': FolderPlus,
  'vault.fs.directories.add': FolderPlus,
  'vault.fs.directories.edit': PenToSquare,
  'vault.fs.directories.rename': PenToSquare,
  'vault.fs.directories.move': ArrowRightArrowLeft,
  'vault.fs.directories.copy': Copy,
  'vault.fs.directories.delete': TrashCan,
  'vault.fs.directories.remove': TrashCan,
  'vault.fs.directories.share': ShareFromSquare,
  'vault.fs.directories.share.internal': ShareNodes,
  'vault.fs.directories.share.public': Globe,

  'vault.storage.view': HardDrive,
  'vault.storage.manage': HardDrive,
  'vault.metadata.view': ObjectGroup,
  'vault.metadata.edit': PenToSquare,
  'vault.tags.view': ObjectGroup,
  'vault.tags.edit': PenToSquare,
  'vault.versions.view': Compress,
  'vault.versions.manage': Compress,

  // Legacy vault aliases
  migrate_data: Cloud,
  manage_access: UserShield,
  manage_tags: ObjectGroup,
  manage_metadata: PenToSquare,
  manage_versions: Compress,
  manage_file_locks: Lock,

  // Admin: audits
  'admin.audits.view': ClipboardList,

  // Admin: identities.users
  'admin.identities.users.view': UsersGear,
  'admin.identities.users.add': UserPlus,
  'admin.identities.users.edit': UserPen,
  'admin.identities.users.delete': UserXmark,

  // Admin: identities.admins
  'admin.identities.admins.view': UserShield,
  'admin.identities.admins.add': UserPlus,
  'admin.identities.admins.edit': UserPen,
  'admin.identities.admins.delete': UserXmark,

  // Admin: identities.groups
  'admin.identities.groups.view': UserGroup,
  'admin.identities.groups.add': Users,
  'admin.identities.groups.edit': UsersGear,
  'admin.identities.groups.delete': UserXmark,
  'admin.identities.groups.add-member': UserPlus,
  'admin.identities.groups.remove-member': UserXmark,
  'admin.identities.groups.view-members': UsersBetweenLines,

  // Admin: vaults.self
  'admin.vaults.self.view': Cloud,
  'admin.vaults.self.view_stats': ChartSimple,
  'admin.vaults.self.create': Plus,
  'admin.vaults.self.edit': PenToSquare,
  'admin.vaults.self.remove': TrashCan,

  // Admin: vaults.admin
  'admin.vaults.admin.view': UserShield,
  'admin.vaults.admin.view_stats': ChartSimple,
  'admin.vaults.admin.create': Plus,
  'admin.vaults.admin.edit': PenToSquare,
  'admin.vaults.admin.remove': TrashCan,

  // Admin: vaults.user
  'admin.vaults.user.view': UsersGear,
  'admin.vaults.user.view_stats': ChartSimple,
  'admin.vaults.user.create': Plus,
  'admin.vaults.user.edit': PenToSquare,
  'admin.vaults.user.remove': TrashCan,

  // Admin: settings
  'admin.settings.websocket.view': DiagramProject,
  'admin.settings.websocket.edit': Sliders,

  'admin.settings.http.view': Globe,
  'admin.settings.http.edit': Sliders,

  'admin.settings.database.view': Database,
  'admin.settings.database.edit': Database,

  'admin.settings.auth.view': ShieldKeyhole,
  'admin.settings.auth.edit': ShieldHalved,

  'admin.settings.logging.view': ClipboardList,
  'admin.settings.logging.edit': Sliders,

  'admin.settings.caching.view': BoxesStacked,
  'admin.settings.caching.edit': Sliders,

  'admin.settings.sharing.view': SquareShareNodes,
  'admin.settings.sharing.edit': Sliders,

  'admin.settings.services.view': Server,
  'admin.settings.services.edit': Gears,

  // Admin: roles.admin
  'admin.roles.admin.view': UserShield,
  'admin.roles.admin.add': UserPlus,
  'admin.roles.admin.edit': UserGear,
  'admin.roles.admin.delete': UserXmark,

  // Admin: roles.vault
  'admin.roles.vault.view': UserLock,
  'admin.roles.vault.add': UserPlus,
  'admin.roles.vault.edit': UserGear,
  'admin.roles.vault.delete': UserXmark,

  // Admin: keys.api.self
  'admin.keys.api.self.view': Key,
  'admin.keys.api.self.create': Plus,
  'admin.keys.api.self.edit': PenToSquare,
  'admin.keys.api.self.remove': TrashCan,
  'admin.keys.api.self.export': ArrowUpFromBracket,
  'admin.keys.api.self.rotate': Gears,
  'admin.keys.api.self.consume': Bolt,

  // Admin: keys.api.admin
  'admin.keys.api.admin.view': KeySkeleton,
  'admin.keys.api.admin.create': Plus,
  'admin.keys.api.admin.edit': PenToSquare,
  'admin.keys.api.admin.remove': TrashCan,
  'admin.keys.api.admin.export': ArrowUpFromBracket,
  'admin.keys.api.admin.rotate': Gears,
  'admin.keys.api.admin.consume': Bolt,

  // Admin: keys.api.user
  'admin.keys.api.user.view': Keyhole,
  'admin.keys.api.user.create': Plus,
  'admin.keys.api.user.edit': PenToSquare,
  'admin.keys.api.user.remove': TrashCan,
  'admin.keys.api.user.export': ArrowUpFromBracket,
  'admin.keys.api.user.rotate': Gears,
  'admin.keys.api.user.consume': Bolt,

  // Admin: keys.encryption
  'admin.keys.encryption.view': Lock,
  'admin.keys.encryption.export': ArrowUpFromBracket,
  'admin.keys.encryption.rotate': Fingerprint,

  // Legacy admin aliases
  manage_admins: UserShield,
  manage_users: UsersGear,
  manage_roles: UserGroup,
  manage_settings: Sliders,
  manage_vaults: Cloud,
  audit_log_access: ClipboardList,
  full_api_key_access: Key,
}
