import { useWs } from '@/lib/query'

// settings.policy.get: the operator settings any signed-in console needs (which share links are allowed, the
// defaults new vaults start with). The full settings document stays super-admin only.
export const useServerPolicy = () => useWs('settings.policy.get', null, { staleTime: 60_000 })
