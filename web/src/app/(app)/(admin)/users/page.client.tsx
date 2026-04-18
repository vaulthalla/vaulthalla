'use client'

import { User } from '@/models/user'
import UserCard from '@/components/users/UserCard'
import { useEffect, useState } from 'react'
import { useAuthStore } from '@/stores/authStore'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'

const UsersClientPage = () => {
  const [users, setUsers] = useState<User[]>([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    const fetchUsers = async () => await useAuthStore.getState().getUsers()
    if (users.length === 0)
      fetchUsers()
        .then(fetchedUsers => {
          setUsers(fetchedUsers)
          setLoading(false)
        })
        .catch(err => {
          console.error('Failed to fetch users:', err)
        })
  }, [users.length])

  if (loading) return <CircleNotchLoader />

  return users.map(user => <UserCard key={user.id} {...user} />)
}

export default UsersClientPage
