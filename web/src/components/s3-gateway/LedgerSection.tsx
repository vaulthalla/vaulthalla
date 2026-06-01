'use client'

import { PriceBudgetLedgerEntry } from '@/models/pricing/priceBudgetLedger'
import type { Vault } from '@/models/vaults'
import { money } from './shared'

export function LedgerSection({ ledger, vaultById }: { ledger: PriceBudgetLedgerEntry[]; vaultById: Map<number, Vault> }) {
  return (
    <div className="overflow-x-auto rounded border border-white/10">
      <table className="min-w-full text-left text-sm">
        <thead className="bg-white/[0.03] text-xs uppercase tracking-normal text-white/45">
          <tr>
            <th className="px-3 py-2">Operation</th>
            <th className="px-3 py-2">Vault</th>
            <th className="px-3 py-2">Cost</th>
            <th className="px-3 py-2">Status</th>
            <th className="px-3 py-2">Source</th>
            <th className="px-3 py-2">Object</th>
          </tr>
        </thead>
        <tbody className="divide-y divide-white/10">
          {ledger.map(row => (
            <tr key={row.id ?? `${row.request_uuid}-${row.created_at}`}>
              <td className="px-3 py-2 text-white">{row.operation ?? '-'}</td>
              <td className="px-3 py-2 text-white/70">{vaultById.get(row.vault_id)?.name ?? row.vault_id}</td>
              <td className="px-3 py-2 text-white/70">{money(row.committed_cost ?? row.reserved_cost, row.currency)}</td>
              <td className="px-3 py-2 text-white/70">{row.status}</td>
              <td className="px-3 py-2 text-white/70" data-testid="s3-gateway-ledger-source">
                {row.usage_source ?? '-'}{row.synthetic ? ' synthetic' : ''}
              </td>
              <td className="max-w-[240px] truncate px-3 py-2 text-white/55">{row.object_key ?? '-'}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}
