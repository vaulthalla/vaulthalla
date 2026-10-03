'use client'

import dynamic from 'next/dynamic'

// The budget editor only loads when someone opens it (keeps the route's first load small).
export const LazyPolicyDialog = dynamic(() => import('@/features/cost/PolicyDialog').then(m => m.PolicyDialog), {
  ssr: false,
})
