import { VaultRole, AdminRole } from '@/models/role'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import RoleCard from '@/components/roles/RoleCard'

const RolesComponent = ({ roles }: { roles: VaultRole[] | AdminRole[] }) => {
  if (!roles) return <CircleNotchLoader />
  if (roles.length === 0) return <p className="text-center text-white/60">No roles found.</p>

  const sortedRoles = roles
    .map(role => ({ role, enabled: role.permissions.reduce((acc, p) => acc + (p.value ? 1 : 0), 0) }))
    .sort(
      (a, b) =>
        b.enabled - a.enabled
        || b.role.permissions.length - a.role.permissions.length
        || a.role.name.localeCompare(b.role.name),
    )
    .map(r => r.role)

  return sortedRoles.map(role => <RoleCard role={role} key={role.id} />)
}

export default RolesComponent
