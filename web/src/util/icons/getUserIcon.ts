import Crown from '@/fa-duotone-regular/crown.svg'
import ShieldHalved from '@/fa-duotone-regular/shield-halved.svg'
import Gavel from '@/fa-duotone-regular/gavel.svg'
import UserIcon from '@/fa-duotone-regular/user.svg'
import UserSecret from '@/fa-duotone-regular/user-secret.svg'

export const getUserIcon = (role: string) => {
  if (role === 'Super Admin') return Crown
  else if (role === 'Admin') return ShieldHalved
  else if (role === 'Moderator') return Gavel
  else if (role === 'User') return UserIcon
  else if (role === 'Guest') return UserSecret
  else return UserIcon
}
