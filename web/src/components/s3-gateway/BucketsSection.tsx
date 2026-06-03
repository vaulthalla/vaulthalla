'use client'

import { useEffect, useMemo, useState } from 'react'
import CheckIcon from '@/fa-duotone/circle-check.svg'
import XIcon from '@/fa-duotone/circle-xmark.svg'
import PlusIcon from '@/fa-duotone/plus.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import VaultIcon from '@/fa-duotone/vault.svg'
import {
  S3GatewayBucketBindPayload,
  S3GatewayBucketBinding,
  S3GatewayCreateLocalBucketPayload,
  S3GatewayCreateRemoteCachePayload,
} from '@/models/s3Gateway'
import type { Vault } from '@/models/vaults'
import { dangerButtonClass, fieldClass, primaryButtonClass, Section } from './shared'

export function BucketsSection({
  buckets,
  vaults,
  onBindBucket,
  onUnbindBucket,
  onCreateLocalBucket,
  onCreateRemoteCacheBucket,
}: {
  buckets: S3GatewayBucketBinding[]
  vaults: Vault[]
  onBindBucket: (payload: S3GatewayBucketBindPayload) => Promise<void>
  onUnbindBucket: (payload: { bucket_name: string }) => Promise<boolean>
  onCreateLocalBucket: (payload: S3GatewayCreateLocalBucketPayload) => Promise<S3GatewayBucketBinding>
  onCreateRemoteCacheBucket: (payload: S3GatewayCreateRemoteCachePayload) => Promise<S3GatewayBucketBinding>
}) {
  const vaultById = useMemo(() => new Map(vaults.map(vault => [vault.id, vault])), [vaults])
  const [bindBucketName, setBindBucketName] = useState('')
  const [bindVaultId, setBindVaultId] = useState('')
  const [bindMode, setBindMode] = useState('local')
  const bindVault = useMemo(() => vaults.find(vault => String(vault.id) === bindVaultId) ?? null, [bindVaultId, vaults])
  const bindModeOptions = useMemo(() => (bindVault?.type === 's3' ? ['remote_cache', 'remote_proxy'] : ['local']), [bindVault])
  const [localBucketName, setLocalBucketName] = useState('')
  const [remoteBucketName, setRemoteBucketName] = useState('')
  const [remoteApiKey, setRemoteApiKey] = useState('')
  const [remoteUpstreamBucket, setRemoteUpstreamBucket] = useState('')

  useEffect(() => {
    if (!bindModeOptions.includes(bindMode)) setBindMode(bindModeOptions[0])
  }, [bindMode, bindModeOptions])

  return (
    <Section title="Bucket Bindings / Routing" icon={VaultIcon}>
      <div className="mb-4 rounded border border-white/10 bg-white/[0.03] p-3 text-sm leading-6 text-white/65" data-testid="s3-gateway-bucket-routing-note">
        Bindings expose a Vaulthalla vault under a downstream S3 bucket name. They do not grant access. Gateway credentials still need user_access principal permissions or role-based credential policy.
      </div>
      <div className="grid gap-3 lg:grid-cols-3">
        <div className="rounded border border-white/10 bg-white/[0.03] p-3">
          <div className="mb-3 text-sm font-medium text-white">Bind vault</div>
          <div className="space-y-2">
            <input className={fieldClass} placeholder="Bucket name" value={bindBucketName} onChange={event => setBindBucketName(event.target.value)} />
            <select className={fieldClass} value={bindVaultId} onChange={event => setBindVaultId(event.target.value)}>
              <option value="">Vault</option>
              {vaults.map(vault => <option key={vault.id} value={vault.id}>{vault.name}</option>)}
            </select>
            <select className={fieldClass} value={bindMode} onChange={event => setBindMode(event.target.value)}>
              {bindModeOptions.map(mode => <option key={mode} value={mode}>{mode}</option>)}
            </select>
            <button className={primaryButtonClass} type="button" disabled={!bindBucketName || !bindVaultId} onClick={() => void onBindBucket({ bucket_name: bindBucketName, vault_id: Number(bindVaultId), mode: bindMode })}>
              <PlusIcon className="h-4 w-4" />
              Bind
            </button>
          </div>
        </div>

        <div className="rounded border border-white/10 bg-white/[0.03] p-3">
          <div className="mb-3 text-sm font-medium text-white">Create local</div>
          <div className="space-y-2">
            <input className={fieldClass} data-testid="s3-gateway-local-bucket-name-input" placeholder="Bucket name" value={localBucketName} onChange={event => setLocalBucketName(event.target.value)} />
            <button className={primaryButtonClass} data-testid="s3-gateway-create-local-bucket" type="button" disabled={!localBucketName} onClick={() => void onCreateLocalBucket({ bucket_name: localBucketName })}>
              <PlusIcon className="h-4 w-4" />
              Create
            </button>
          </div>
        </div>

        <div className="rounded border border-white/10 bg-white/[0.03] p-3">
          <div className="mb-3 text-sm font-medium text-white">Create remote-cache</div>
          <div className="space-y-2">
            <input className={fieldClass} placeholder="Bucket name" value={remoteBucketName} onChange={event => setRemoteBucketName(event.target.value)} />
            <input className={fieldClass} placeholder="API key id or name" value={remoteApiKey} onChange={event => setRemoteApiKey(event.target.value)} />
            <input className={fieldClass} placeholder="Upstream bucket" value={remoteUpstreamBucket} onChange={event => setRemoteUpstreamBucket(event.target.value)} />
            <button
              className={primaryButtonClass}
              type="button"
              disabled={!remoteBucketName || !remoteApiKey || !remoteUpstreamBucket}
              onClick={() => void onCreateRemoteCacheBucket({
                bucket_name: remoteBucketName,
                api_key: remoteApiKey,
                upstream_bucket: remoteUpstreamBucket,
                encrypt_upstream: true,
              })}>
              <PlusIcon className="h-4 w-4" />
              Create
            </button>
          </div>
        </div>
      </div>

      <div className="mt-4 overflow-x-auto">
        <table className="min-w-full text-left text-sm">
          <thead className="text-xs uppercase tracking-normal text-white/45">
            <tr>
              <th className="px-3 py-2">Bucket</th>
              <th className="px-3 py-2">Vault</th>
              <th className="px-3 py-2">Mode</th>
              <th className="px-3 py-2">API exclusive</th>
              <th className="px-3 py-2"></th>
            </tr>
          </thead>
          <tbody className="divide-y divide-white/10">
            {buckets.map(bucket => (
              <tr key={bucket.bucket_name}>
                <td className="px-3 py-2 font-medium text-white" data-testid="s3-gateway-bucket-name">{bucket.bucket_name}</td>
                <td className="px-3 py-2 text-white/70">{vaultById.get(bucket.vault_id)?.name ?? bucket.vault_id}</td>
                <td className="px-3 py-2 text-white/70">{bucket.mode}</td>
                <td className="px-3 py-2">{bucket.api_exclusive ? <CheckIcon className="h-4 w-4 text-emerald-300" /> : <XIcon className="h-4 w-4 text-white/35" />}</td>
                <td className="px-3 py-2">
                  <button className={dangerButtonClass} type="button" onClick={() => void onUnbindBucket({ bucket_name: bucket.bucket_name })}>
                    <TrashIcon className="h-4 w-4" />
                    Unbind
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </Section>
  )
}
