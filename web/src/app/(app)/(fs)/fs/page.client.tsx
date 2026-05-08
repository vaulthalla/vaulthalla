'use client'

import { Filesystem } from '@/components/fs/Filesystem'
import { useFSStore } from '@/stores/fsStore'
import React from 'react'

const FSClientPage = () => {
  const files = useFSStore(state => state.files)

  return <Filesystem files={files} />
}

export default FSClientPage
