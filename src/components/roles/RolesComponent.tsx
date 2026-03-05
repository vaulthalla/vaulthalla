import { VaultRole, UserRole } from '@/models/role'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import RoleCard from '@/components/roles/RoleCard'

const RolesComponent = ({ roles }: { roles: VaultRole[] | UserRole[] }) => {
  if (!roles) return <CircleNotchLoader />
  if (roles.length === 0) return <p className="text-center text-white/60">No roles found.</p>
  return roles.map(role => <RoleCard {...role} key={role.name} />)
}

export default RolesComponent
