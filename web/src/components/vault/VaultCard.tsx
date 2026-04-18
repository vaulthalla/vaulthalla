'use client'

import { getVaultType, S3Vault, Vault } from '@/models/vaults'
import ShieldCheck from '@/fa-regular/shield-check.svg'
import AlertTriangle from '@/fa-regular/triangle-exclamation.svg'
import * as motion from 'motion/react-client'
import { getVaultIcon } from '@/util/icons/vaultIconsMap'
import { useEffect, useState } from 'react'
import { useApiKeyStore } from '@/stores/apiKeyStore'
import Link from 'next/link'

const VaultCard = (vault: Vault) => {
  const [provider, setProvider] = useState('')

  console.log('vault', vault)

  useEffect(() => {
    const getProviderName = async () => {
      if (vault.type !== 's3') setProvider('local')
      else {
        try {
          const s3Vault = new S3Vault(vault)
          const key = await useApiKeyStore.getState().getApiKey({ id: s3Vault.api_key_id })
          if (key.provider) setProvider(key.provider)
        } catch (error) {
          console.error('Error fetching provider:', error)
          setProvider('local')
        }
      }
    }

    getProviderName()
  }, [vault])

  const Icon = getVaultIcon({ type: vault.type, provider: provider })

  return (
    <Link href={`/vaults/${vault.id}`} key={vault.id}>
      <motion.div
        initial={{ opacity: 0, y: 12 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.35, ease: 'easeOut' }}
        whileHover={{ scale: 1.015 }}
        whileTap={{ scale: 0.985 }}
        className="card-glass relative my-4 w-96 max-w-full rounded-xl border p-6 shadow-md transition-transform">
        <div className="absolute top-2.5 right-2.5 text-2xl">
          {vault.is_active ?
            <ShieldCheck className="fill-current text-green-600" />
          : <AlertTriangle className="fill-current text-red-600" />}
        </div>

        <div className="flex items-center space-x-2 text-2xl">
          <Icon className="text-primary fill-current" />
          <h2 className="font-bold tracking-tight text-white">{vault.name}</h2>
        </div>

        <div className="text-md mt-4 text-gray-50">
          <p>
            <span className="font-medium text-gray-300">Owner:</span> {vault.owner}
          </p>
          <p>
            <span className="font-medium text-gray-300">Type:</span> {getVaultType(vault.type)}
          </p>
          <p>
            <span className="font-medium text-gray-300">Created:</span>{' '}
            <span>{new Date(vault.created_at).toLocaleString()}</span>
          </p>
          <p>
            <span className="font-medium text-gray-300">Status:</span>{' '}
            <span className={vault.is_active ? 'font-semibold text-green-600' : 'font-semibold text-red-600'}>
              {vault.is_active ? 'Active' : 'Inactive'}
            </span>
          </p>
        </div>
      </motion.div>
    </Link>
  )
}

export default VaultCard
