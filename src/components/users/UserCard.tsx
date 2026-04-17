'use client'

import { User } from '@/models/user'
import * as motion from 'motion/react-client'
import { getUserIcon } from '@/util/icons/getUserIcon'
import { prettifySnakeCase } from '@/util/prettifySnakeCase'
import Link from 'next/link'

const UserCard = (user: User) => {
  const roleName = prettifySnakeCase(user.admin_role.name)
  const Icon = getUserIcon(roleName)

  return (
    <Link href={`/users/${user.name}`} key={user.id} className="w-full">
      <motion.div
        initial={{ opacity: 0, y: -20, scale: 0.95 }}
        animate={{ opacity: 1, y: 0, scale: 1 }}
        whileHover={{ scale: 1.02 }}
        transition={{ duration: 0.3, ease: 'easeOut' }}
        className="card-glass my-4 w-full rounded-2xl border border-white/10 p-6 text-left shadow-xl backdrop-blur-md transition-transform duration-300 hover:shadow-2xl">
        <div className="mb-4 flex items-center justify-between text-2xl">
          <h3 className="font-semibold tracking-tight text-white">{user.name}</h3>
          <Icon className="text-primary fill-current" />
        </div>
        <div className="text-md space-y-1 text-gray-300">
          <div className="flex w-full items-center justify-between">
            <p>
              <span className="font-medium text-gray-400">Email:</span> {user.email ?? 'N/A'}
            </p>
            <p>
              <span className="font-medium text-gray-400">Role:</span> {roleName}
            </p>
          </div>

          <div className="flex w-full items-center justify-between">
            <p>
              <span className="font-medium text-gray-400">Last Login:</span>{' '}
              {new Date(user.last_login).toLocaleString()}
            </p>
            <p>
              <span className="font-medium text-gray-400">Created:</span>{' '}
              {new Date(user.created_at).toLocaleDateString()}
            </p>
          </div>
        </div>
      </motion.div>
    </Link>
  )
}

export default UserCard
