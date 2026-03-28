'use client'

import { APIKey } from '@/models/apiKey'
import * as motion from 'motion/react-client'
import AWS from '@/fa-brands/aws.svg'
import Cloudflare from '@/fa-brands/cloudflare.svg'
import { FC, SVGProps } from 'react'
import Link from 'next/link'

interface ProviderIconMapType {
  [key: string]: FC<SVGProps<SVGSVGElement>>
}

const APIKeyCard = (key: APIKey) => {
  const providerIconMap: ProviderIconMapType = { AWS: AWS, 'Cloudflare R2': Cloudflare }

  let ProviderIcon: FC<SVGProps<SVGSVGElement>> | undefined
  if (key.provider && providerIconMap[key.provider]) ProviderIcon = providerIconMap[key.provider]

  return (
    <Link href="/api-keys/[slug]" as={`/api-keys/${key.api_key_id}`}>
      <motion.div
        initial={{ opacity: 0, y: 12 }}
        animate={{ opacity: 1, y: 0 }}
        transition={{ duration: 0.3, ease: 'easeOut' }}
        className="group rounded-2xl border border-white/10 bg-white/10 p-5 shadow-md backdrop-blur-md transition duration-300 hover:bg-white/20 hover:shadow-lg hover:backdrop-blur-xl">
        <div className="flex justify-between align-middle">
          <h3 className="text-3xl font-semibold text-white transition-colors duration-200 group-hover:text-cyan-300">
            {key.name}
          </h3>
          {ProviderIcon && (
            <ProviderIcon className="-mt-2.5 fill-current text-6xl text-white transition-colors duration-200 group-hover:text-cyan-300" />
          )}
        </div>
        <div className="mt-2 flex justify-between text-lg text-gray-300">
          <p>
            <span className="font-medium text-gray-400">Created:</span> {new Date(key.created_at).toLocaleDateString()}
          </p>
          <p>
            <span className="font-medium text-gray-400">Type:</span> {key.type}
          </p>
        </div>
      </motion.div>
    </Link>
  )
}

export default APIKeyCard
