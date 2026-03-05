import { VaultRole, UserRole } from '@/models/role'
import { motion } from 'framer-motion'
import { permissionIconMap } from '@/util/icons/permissionIconMap'
import { prettifySnakeCase } from '@/util/prettifySnakeCase'
import Link from 'next/link'

const RoleCard = (role: VaultRole | UserRole) => {
  const categorizedPerms: Record<string, string[]> = {}

  if (role.type === 'user') categorizedPerms['User Permissions'] = Object.keys(role.permissions)
  else categorizedPerms['Vault Permissions'] = Object.keys(role.permissions)

  const Tooltip = ({ children, label }: { children: React.ReactNode; label: string }) => (
    <div className="group relative">
      {children}
      <div className="absolute bottom-full mb-1 hidden w-max max-w-xs rounded bg-black/80 px-2 py-1 text-xs text-white group-hover:block">
        {label}
      </div>
    </div>
  )

  const transformDisplayName = (name: string) =>
    name
      .replace(/_/g, ' ')
      .replace(/\b\w/g, char => char.toUpperCase())
      .replace(/([a-z])([A-Z])/g, '$1 $2')

  return (
    <Link href="/roles/[role_id]" as={`/roles/${role.name}`} key={role.name}>
      <motion.div
        initial={{ opacity: 0, y: 10 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.3, ease: 'easeOut' }}
        className="mb-4 rounded-2xl border border-white/10 bg-white/10 p-5 shadow-xl backdrop-blur-md transition hover:scale-[1.01]">
        <h3 className="mb-1 text-xl font-semibold text-white">{prettifySnakeCase(role.name)}</h3>
        <p className="mb-4 text-sm text-white/70">{role.description || 'No description provided'}</p>

        <div className="space-y-4">
          {Object.entries(categorizedPerms).map(([category, perms]) =>
            perms.length > 0 ?
              <div key={category}>
                <h4 className="mb-1 text-sm font-bold tracking-wide text-white/60 uppercase">{category}</h4>
                <div className="flex flex-wrap items-center gap-2 rounded-lg bg-black/20 p-3 sm:gap-3">
                  {perms.map((perm, index) => {
                    const Icon = permissionIconMap[perm]
                    const label = transformDisplayName(perm)
                    return Icon ?
                        <Tooltip key={index} label={label}>
                          <Icon
                            className={`${role.permissions[perm] ? 'text-glow-orange' : 'text-muted'} fill-current text-xl transition-transform hover:scale-110`}
                            aria-label={label}
                          />
                        </Tooltip>
                      : <div key={index} className="text-xl text-white/30" title={label}>
                          •
                        </div>
                  })}
                </div>
              </div>
            : null,
          )}
        </div>
      </motion.div>
    </Link>
  )
}

export default RoleCard
