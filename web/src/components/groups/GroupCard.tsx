'use client'

import * as motion from 'motion/react-client'
import { Group } from '@/models/group'

const GroupCard = (group: Group) => {
  return (
    <motion.div
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.3, ease: 'easeOut' }}
      className="mb-4 rounded-2xl border border-white/10 bg-white/10 p-5 shadow-xl backdrop-blur-md transition hover:scale-[1.01]">
      <h3 className="mb-1 text-xl font-semibold text-white">{group.name}</h3>
      <p className="mb-4 text-sm text-white/70">{group.description || 'No description provided'}</p>

      <div className="text-md text-gray-300">
        <strong>Members:</strong> <span className="text-white">{group.members.length}</span>
      </div>
    </motion.div>
  )
}

export default GroupCard
