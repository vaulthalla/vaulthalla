'use client'

import CheckIcon from '@/fa-duotone/circle-check.svg'
import XIcon from '@/fa-duotone/circle-xmark.svg'
import CopyIcon from '@/fa-duotone/copy.svg'
import EyeIcon from '@/fa-duotone/eye.svg'
import KeyIcon from '@/fa-duotone/key.svg'
import PlusIcon from '@/fa-duotone/plus.svg'
import TrashIcon from '@/fa-duotone/trash.svg'
import { S3GatewayCredential } from '@/models/s3Gateway'
import { buttonClass, dangerButtonClass, formatDate, primaryButtonClass, scopeLabel, Section } from './shared'

export function CredentialsSection({
  credentials,
  selectedCredential,
  createdSecret,
  onCreateOpen,
  onSelect,
  onRevoke,
  onCopy,
  onHideSecret,
}: {
  credentials: S3GatewayCredential[]
  selectedCredential: S3GatewayCredential | null
  createdSecret: { credential: S3GatewayCredential; secret_access_key: string } | null
  onCreateOpen: () => void
  onSelect: (id: number) => void
  onRevoke: (accessKey: string) => void
  onCopy: (text: string) => void
  onHideSecret: () => void
}) {
  return (
    <Section
      title="Credentials"
      icon={KeyIcon}
      right={
        <button className={primaryButtonClass} data-testid="s3-gateway-open-create-credential" type="button" onClick={onCreateOpen}>
          <PlusIcon className="h-4 w-4" />
          Create
        </button>
      }>
      {createdSecret && (
        <div className="mb-4 rounded border border-emerald-300/25 bg-emerald-500/10 p-3 text-sm text-emerald-50" data-testid="s3-gateway-secret-panel">
          <div className="mb-2 flex items-center gap-2 font-medium">
            <EyeIcon className="h-4 w-4" />
            Secret access key
          </div>
          <div className="grid gap-2 md:grid-cols-[1fr_auto]">
            <code className="overflow-x-auto rounded bg-black/35 p-2 text-xs">{createdSecret.secret_access_key}</code>
            <button className={buttonClass} type="button" onClick={() => onCopy(createdSecret.secret_access_key)}>
              <CopyIcon className="h-4 w-4" />
              Copy
            </button>
          </div>
          <button className="mt-2 text-xs text-emerald-100/75 underline" data-testid="s3-gateway-hide-secret" type="button" onClick={onHideSecret}>
            Hide
          </button>
        </div>
      )}

      <div className="overflow-x-auto">
        <table className="min-w-full text-left text-sm">
          <thead className="text-xs uppercase tracking-normal text-white/45">
            <tr>
              <th className="px-3 py-2">Name</th>
              <th className="px-3 py-2">Access key</th>
              <th className="px-3 py-2">Principal</th>
              <th className="px-3 py-2">Scope</th>
              <th className="px-3 py-2">Local budget</th>
              <th className="px-3 py-2">Enabled</th>
              <th className="px-3 py-2">Expires</th>
              <th className="px-3 py-2">Last used</th>
              <th className="px-3 py-2"></th>
            </tr>
          </thead>
          <tbody className="divide-y divide-white/10">
            {credentials.map(credential => (
              <tr key={credential.id} className={selectedCredential?.id === credential.id ? 'bg-cyan-400/10' : ''}>
                <td className="px-3 py-2 font-medium text-white" data-testid="s3-gateway-credential-name">{credential.name}</td>
                <td className="px-3 py-2 font-mono text-xs text-white/70">{credential.access_key}</td>
                <td className="px-3 py-2 text-white/70">{credential.principal_user_id}</td>
                <td className="px-3 py-2 text-white/70">{scopeLabel(credential.scope_mode)}</td>
                <td className="px-3 py-2">{credential.enforce_budget_for_local_requests ? <CheckIcon className="h-4 w-4 text-emerald-300" /> : <XIcon className="h-4 w-4 text-white/30" />}</td>
                <td className="px-3 py-2">{credential.enabled ? <CheckIcon className="h-4 w-4 text-emerald-300" /> : <XIcon className="h-4 w-4 text-red-300" />}</td>
                <td className="px-3 py-2 text-white/55">{formatDate(credential.expires_at)}</td>
                <td className="px-3 py-2 text-white/55">{formatDate(credential.last_used_at)}</td>
                <td className="px-3 py-2">
                  <div className="flex gap-2">
                    <button className={buttonClass} type="button" onClick={() => onSelect(credential.id)}>
                      <EyeIcon className="h-4 w-4" />
                      Select
                    </button>
                    <button className={dangerButtonClass} type="button" onClick={() => onRevoke(credential.access_key)}>
                      <TrashIcon className="h-4 w-4" />
                      Revoke
                    </button>
                  </div>
                </td>
              </tr>
            ))}
            {credentials.length === 0 && (
              <tr>
                <td className="px-3 py-6 text-center text-white/50" colSpan={9}>No gateway credentials</td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </Section>
  )
}
