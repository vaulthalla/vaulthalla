'use client'

import { useApiKeyStore } from '@/stores/apiKeyStore'
import APIKeyCard from '@/components/api_keys/APIKeyCard'

const APIKeysClientPage = () => {
  const { apiKeys } = useApiKeyStore()

  if (!apiKeys || apiKeys.length === 0)
    return <p className="text-gray-500">No API keys found. Create one to get started.</p>

  return apiKeys.map(apiKey => <APIKeyCard key={apiKey.api_key_id} {...apiKey} />)
}

export default APIKeysClientPage
