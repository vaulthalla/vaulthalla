'use client'

import React, { useEffect, useMemo, useState } from 'react'
import { getVaultType, S3Vault, Vault } from '@/models/vaults'
import ShieldCheck from '@/fa-regular/shield-check.svg'
import AlertTriangle from '@/fa-regular/triangle-exclamation.svg'
import * as motion from 'motion/react-client'
import { getVaultIcon } from '@/util/icons/vaultIconsMap'
import { useApiKeyStore } from '@/stores/apiKeyStore'

export type VaultHeroProps = {
  vault: Vault
  rightSlot?: React.ReactNode
  usedBytes?: number
  totalBytes?: number
}

const Chip = ({ children }: { children: React.ReactNode }) => (
  <span className="inline-flex items-center rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-[11px] text-white/70">
    {children}
  </span>
)

function pct(used?: number, total?: number) {
  if (!total || total <= 0 || used == null) return null
  return Math.min(100, Math.max(0, (used / total) * 100))
}

const VaultHeroClient = ({ vault, rightSlot, usedBytes, totalBytes }: VaultHeroProps) => {
  const [provider, setProvider] = useState<string>('')

  useEffect(() => {
    let alive = true
    const run = async () => {
      if (vault.type !== 's3') return alive && setProvider('local')
      try {
        const s3Vault = new S3Vault(vault)
        const key = await useApiKeyStore.getState().getApiKey({ id: s3Vault.api_key_id })
        if (alive) setProvider(key?.provider ?? 's3')
      } catch {
        if (alive) setProvider('s3')
      }
    }
    void run()
    return () => {
      alive = false
    }
  }, [vault])

  const typeLabel = useMemo(() => getVaultType(vault.type), [vault.type])
  const Icon = getVaultIcon({ type: vault.type, provider })
  const p = pct(usedBytes, totalBytes)

  return (
    <motion.section
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.35, ease: 'easeOut' }}
      className="relative w-full overflow-hidden rounded-2xl border border-white/10 bg-zinc-950/60 shadow-[0_20px_60px_-35px_rgba(0,0,0,0.9)] backdrop-blur-md">
      <div
        aria-hidden
        className="pointer-events-none absolute inset-0 bg-[radial-gradient(900px_circle_at_0%_0%,rgba(56,189,248,0.10),transparent_55%), radial-gradient(900px_circle_at_100%_0%,rgba(168,85,247,0.10),transparent_55%)]"
      />

      <div className="relative z-10 flex w-full flex-col gap-3 px-5 py-4 md:flex-row md:items-center">
        <div className="flex min-w-0 items-center gap-3">
          <div className="flex h-12 w-12 shrink-0 items-center justify-center rounded-xl border border-white/10 bg-white/5">
            <Icon className="text-primary h-6 w-6 fill-current" />
          </div>

          <div className="min-w-0">
            <div className="flex items-center gap-2">
              <h1 className="truncate text-lg font-semibold tracking-tight text-white">{vault.name}</h1>
              <span className="md:hidden">
                {vault.is_active ?
                  <span className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-green-400">
                    <ShieldCheck className="h-3.5 w-3.5 fill-current text-green-500" /> Active
                  </span>
                : <span className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-2.5 py-1 text-xs text-red-400">
                    <AlertTriangle className="h-3.5 w-3.5 fill-current text-red-500" /> Inactive
                  </span>
                }
              </span>
            </div>

            <div className="mt-1 flex flex-wrap items-center gap-2 text-xs text-white/55">
              <span>{typeLabel}</span>
              <span className="text-white/25">•</span>
              <Chip>
                Owner&nbsp;<span className="text-white/85">{vault.owner}</span>
              </Chip>
              <Chip>
                Provider&nbsp;<span className="text-white/85">{provider || '—'}</span>
              </Chip>
              <Chip>
                Created&nbsp;
                <span className="text-white/85">{new Date(vault.created_at).toLocaleDateString()}</span>
              </Chip>
            </div>
          </div>
        </div>

        <div className="md:mx-6 md:flex md:flex-1 md:flex-col md:justify-center">
          <div className="hidden md:block">
            <div className="flex items-center justify-between text-[11px] text-white/45">
              <span>Vault health</span>
              <span>{p != null ? `${p.toFixed(1)}% used` : '—'}</span>
            </div>

            <div className="mt-2 h-2 w-full rounded-full bg-white/10">
              <div className="h-2 rounded-full bg-white/35" style={{ width: p == null ? '18%' : `${p}%` }} />
            </div>

            <div className="mt-3 h-px w-full bg-linear-to-r from-transparent via-white/12 to-transparent" />
          </div>
        </div>

        <div className="hidden items-center gap-3 md:flex">
          <div className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-sm">
            {vault.is_active ?
              <>
                <ShieldCheck className="h-4 w-4 fill-current text-green-500" />
                <span className="font-medium text-green-400">Active</span>
              </>
            : <>
                <AlertTriangle className="h-4 w-4 fill-current text-red-500" />
                <span className="font-medium text-red-400">Inactive</span>
              </>
            }
          </div>
          {rightSlot ?? null}
        </div>
      </div>
    </motion.section>
  )
}

export default VaultHeroClient
