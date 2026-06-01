'use client'

import { useEffect, useMemo, useState } from 'react'
import { AdminPage } from '@/components/admin/AdminPage'
import { BudgetSection } from '@/components/s3-gateway/BudgetSection'
import { BucketsSection } from '@/components/s3-gateway/BucketsSection'
import { ClientSnippets } from '@/components/s3-gateway/ClientSnippets'
import { CredentialCreateModal } from '@/components/s3-gateway/CredentialCreateModal'
import { CredentialsSection } from '@/components/s3-gateway/CredentialsSection'
import { ScopeEditor } from '@/components/s3-gateway/ScopeEditor'
import { ServiceCard } from '@/components/s3-gateway/ServiceCard'
import { buttonClass } from '@/components/s3-gateway/shared'
import RefreshIcon from '@/fa-duotone/arrows-rotate.svg'
import { Vault } from '@/models/vaults'
import { useS3GatewayStore } from '@/stores/s3GatewayStore'
import { useVaultStore } from '@/stores/vaultStore'

export default function S3GatewayPage() {
  const {
    status,
    credentials,
    scopesByCredentialId,
    buckets,
    policies,
    ledger,
    budgetStatus,
    createdSecret,
    loading,
    saving,
    error,
    fetchStatus,
    fetchCredentials,
    createCredential,
    revokeCredential,
    updateCredentialScope,
    fetchCredentialScopes,
    fetchBuckets,
    bindBucket,
    unbindBucket,
    createLocalBucket,
    createRemoteCacheBucket,
    fetchPolicies,
    upsertPolicy,
    disablePolicy,
    fetchLedger,
    fetchBudgetStatus,
    clearCreatedSecret,
  } = useS3GatewayStore()
  const { vaults, fetchVaults } = useVaultStore()
  const [selectedCredentialId, setSelectedCredentialId] = useState<number | null>(null)
  const [createOpen, setCreateOpen] = useState(false)

  const selectedCredential = useMemo(
    () => credentials.find(item => item.id === selectedCredentialId) ?? credentials[0] ?? null,
    [credentials, selectedCredentialId],
  )
  const selectedScopes = useMemo(
    () => (selectedCredential ? scopesByCredentialId[selectedCredential.id] ?? [] : []),
    [scopesByCredentialId, selectedCredential],
  )
  const gatewayVaults = useMemo(() => {
    const byId = new Map<number, Vault>()
    for (const vault of vaults) byId.set(vault.id, vault)
    for (const bucket of buckets) {
      if (byId.has(bucket.vault_id)) continue
      byId.set(bucket.vault_id, new Vault({
        id: bucket.vault_id,
        name: bucket.bucket_name,
        type: bucket.mode === 'remote_cache' || bucket.mode === 'remote_proxy' ? 's3' : 'local',
        is_active: true,
      }))
    }
    return [...byId.values()]
  }, [buckets, vaults])
  const endpoint = status?.endpoint || '127.0.0.1:9000'
  const snippetAccessKey = createdSecret?.credential.access_key || selectedCredential?.access_key || 'VH_ACCESS_KEY'
  const snippetSecret = createdSecret?.secret_access_key || 'VH_SECRET_ACCESS_KEY'

  useEffect(() => {
    void Promise.all([
      fetchStatus(),
      fetchCredentials(),
      fetchBuckets(),
      fetchPolicies({ include_inactive: true }),
      fetchLedger({ limit: 25 }),
      fetchBudgetStatus({ limit: 25 }),
      fetchVaults(),
    ]).catch(() => undefined)
  }, [fetchBuckets, fetchBudgetStatus, fetchCredentials, fetchLedger, fetchPolicies, fetchStatus, fetchVaults])

  useEffect(() => {
    if (!selectedCredential) return
    void fetchCredentialScopes({ access_key: selectedCredential.access_key }).catch(() => undefined)
    void fetchPolicies({ gateway_credential_id: selectedCredential.id, include_inactive: true }).catch(() => undefined)
    void fetchLedger({ gateway_credential_id: selectedCredential.id, limit: 25 }).catch(() => undefined)
    void fetchBudgetStatus({ gateway_credential_id: selectedCredential.id, limit: 25 }).catch(() => undefined)
  }, [selectedCredential, fetchBudgetStatus, fetchCredentialScopes, fetchLedger, fetchPolicies])

  const refreshAll = async () => {
    await Promise.all([
      fetchStatus(),
      fetchCredentials(),
      fetchBuckets(),
      fetchPolicies(selectedCredential ? { gateway_credential_id: selectedCredential.id, include_inactive: true } : { include_inactive: true }),
      fetchLedger(selectedCredential ? { gateway_credential_id: selectedCredential.id, limit: 25 } : { limit: 25 }),
      fetchBudgetStatus(selectedCredential ? { gateway_credential_id: selectedCredential.id, limit: 25 } : { limit: 25 }),
      fetchVaults(),
    ])
  }

  const copy = (text: string) => {
    void navigator.clipboard?.writeText(text)
  }

  // TODO: Add the dedicated Playwright validation pass for this page's auth, navigation, credential, scope, bucket, budget, and ledger flows.
  return (
    <AdminPage title="S3 Gateway" description="Scoped S3-compatible access, bucket bindings, and gateway cost controls.">
      <div className="space-y-5">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div className="text-sm text-white/55">{error || (loading ? 'Loading gateway state' : '')}</div>
          <button className={buttonClass} type="button" disabled={loading} onClick={() => void refreshAll()}>
            <RefreshIcon className="h-4 w-4" />
            Refresh
          </button>
        </div>

        <ServiceCard status={status} endpoint={endpoint} />
        <CredentialsSection
          credentials={credentials}
          selectedCredential={selectedCredential}
          createdSecret={createdSecret}
          onCreateOpen={() => setCreateOpen(true)}
          onSelect={setSelectedCredentialId}
          onRevoke={access_key => void revokeCredential({ access_key })}
          onCopy={copy}
          onHideSecret={clearCreatedSecret}
        />
        <CredentialCreateModal
          open={createOpen}
          saving={saving}
          vaults={gatewayVaults}
          onClose={() => setCreateOpen(false)}
          onCreate={createCredential}
          onCreated={setSelectedCredentialId}
        />
        <ScopeEditor
          selectedCredential={selectedCredential}
          selectedScopes={selectedScopes}
          vaults={gatewayVaults}
          saving={saving}
          onSave={updateCredentialScope}
        />
        <BucketsSection
          buckets={buckets}
          vaults={gatewayVaults}
          onBindBucket={bindBucket}
          onUnbindBucket={unbindBucket}
          onCreateLocalBucket={async payload => {
            const bucket = await createLocalBucket(payload)
            await fetchVaults().catch(() => undefined)
            return bucket
          }}
          onCreateRemoteCacheBucket={async payload => {
            const bucket = await createRemoteCacheBucket(payload)
            await fetchVaults().catch(() => undefined)
            return bucket
          }}
        />
        <BudgetSection
          selectedCredential={selectedCredential}
          policies={policies}
          ledger={ledger}
          budgetStatus={budgetStatus}
          vaults={gatewayVaults}
          saving={saving}
          onUpsertPolicy={upsertPolicy}
          onDisablePolicy={disablePolicy}
        />
        <ClientSnippets
          endpoint={endpoint}
          accessKey={snippetAccessKey}
          secretKey={snippetSecret}
          buckets={buckets}
          onCopy={copy}
        />
      </div>
    </AdminPage>
  )
}
