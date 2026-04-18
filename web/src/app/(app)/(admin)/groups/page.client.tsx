'use client'

import { useGroupStore } from '@/stores/groupStore'
import GroupCard from '@/components/groups/GroupCard'

const GroupsClientPage = () => {
  const { groups } = useGroupStore()

  if (!groups || groups.length === 0) return <p className="text-center text-white/60">No groups found.</p>

  return groups.map(group => <GroupCard {...group} key={group.id} />)
}

export default GroupsClientPage
