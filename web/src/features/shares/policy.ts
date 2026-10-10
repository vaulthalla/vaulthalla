import { useServerPolicy } from '@/lib/serverPolicy'
import type { ShareAccessMode } from '@/models/linkShare'
import type { ServerPolicy } from '@/models/settings'

// The operator's sharing switches (config sharing.*), from settings.policy.get. The daemon enforces them on every
// create and every use; the console only hides what would be refused. Until the policy loads (or if the daemon
// can't answer), nothing is hidden: the daemon still refuses with a readable message.
export interface SharingPolicy {
  loaded: boolean
  // Sharing as a whole (sharing.enabled).
  enabled: boolean
  // Link kinds that can be created now, in the order the share dialog offers them.
  modes: ShareAccessMode[]
}

export const sharingPolicyFrom = (policy?: ServerPolicy | null): SharingPolicy => {
  const sharing = policy?.sharing
  if (!sharing) return { loaded: false, enabled: true, modes: ['public', 'email_validated'] }
  const enabled = sharing.enabled !== false
  const modes: ShareAccessMode[] = []
  if (enabled && sharing.enable_anonymous !== false) modes.push('public')
  if (enabled && sharing.enable_email_validated !== false) modes.push('email_validated')
  return { loaded: true, enabled, modes }
}

export const useSharingPolicy = (): SharingPolicy => sharingPolicyFrom(useServerPolicy().data?.policy)

// One line for pages that list links, or null when every kind of link works.
export const sharingNotice = (policy: SharingPolicy): string | null => {
  if (!policy.loaded) return null
  if (!policy.enabled) return 'Sharing is turned off on this server. Links can’t be created or opened until an administrator turns it back on; existing links are kept.'
  if (!policy.modes.includes('public'))
    return 'Anyone-with-the-link shares are turned off on this server. Existing ones can’t be opened until they are turned back on.'
  if (!policy.modes.includes('email_validated'))
    return 'Verified-email shares are turned off on this server. Existing ones can’t be opened until they are turned back on.'
  return null
}
