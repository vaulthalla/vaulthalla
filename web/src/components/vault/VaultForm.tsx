'use client'

import { useApiKeyStore } from '@/stores/apiKeyStore'
import { useVaultStore } from '@/stores/vaultStore'
import {
  Controller,
  useForm,
  useWatch,
  type Control,
  type UseFormRegister,
  type UseFormSetValue,
} from 'react-hook-form'
import * as motion from 'motion/react-client'
import { Button } from '@/components/Button'
import {
  LocalDiskVault,
  RemoteSyncPolicy,
  S3BudgetPreset,
  S3RequestBudget,
  S3Vault,
  Vault,
  VaultType,
} from '@/models/vaults'
import { useRouter } from 'next/navigation'
import { useEffect, useMemo, useState } from 'react'
import VaultIcon from '@/fa-duotone/vault.svg'

type FormSyncPolicy = Omit<RemoteSyncPolicy, 'interval'> & { interval: number }

type VaultFormValues = {
  name: string
  type: VaultType
  mount_point?: string
  api_key_id?: number
  bucket?: string
  storage_tier_id?: string | null
  encrypt_upstream?: boolean
  budget_preset?: S3BudgetPreset
  sync?: FormSyncPolicy
}

type BudgetKey = keyof S3RequestBudget
type S3PresetWithoutCustom = Exclude<S3BudgetPreset, 'custom'>

const S3_BUDGET_PRESETS: Record<S3PresetWithoutCustom, S3RequestBudget> = {
  conservative: {
    list_requests: 10,
    head_requests: 100,
    get_requests: 100,
    put_requests: 100,
    copy_requests: 20,
    delete_requests: 100,
    downloaded_bytes: 1024 * 1024 * 1024,
  },
  balanced: {
    list_requests: 100,
    head_requests: 1000,
    get_requests: 1000,
    put_requests: 1000,
    copy_requests: 100,
    delete_requests: 1000,
    downloaded_bytes: 10 * 1024 * 1024 * 1024,
  },
  bulk: {
    list_requests: 1000,
    head_requests: 10000,
    get_requests: 10000,
    put_requests: 10000,
    copy_requests: 1000,
    delete_requests: 10000,
    downloaded_bytes: 100 * 1024 * 1024 * 1024,
  },
  unlimited: {
    list_requests: null,
    head_requests: null,
    get_requests: null,
    put_requests: null,
    copy_requests: null,
    delete_requests: null,
    downloaded_bytes: null,
  },
}

const BUDGET_FIELDS: Array<{ key: BudgetKey; label: string }> = [
  { key: 'list_requests', label: 'LIST requests' },
  { key: 'head_requests', label: 'HEAD requests' },
  { key: 'get_requests', label: 'GET requests' },
  { key: 'put_requests', label: 'PUT requests' },
  { key: 'copy_requests', label: 'COPY requests' },
  { key: 'delete_requests', label: 'DELETE requests' },
  { key: 'downloaded_bytes', label: 'Downloaded bytes' },
]

const DEFAULT_SYNC_POLICY: FormSyncPolicy = {
  strategy: 'cache',
  conflict_policy: 'keep_local',
  interval: 300,
  enabled: true,
  s3_request_budget: S3_BUDGET_PRESETS.balanced,
  max_remote_index_age_seconds: 24 * 60 * 60,
}

const storageTierOptionsForProvider = (provider?: string) => {
  if (provider === 'AWS') {
    return [
      { value: '', label: 'Provider default' },
      { value: 'standard', label: 'Standard' },
      { value: 'standard_ia', label: 'Standard-IA' },
    ]
  }

  if (provider === 'Cloudflare R2') {
    return [
      { value: '', label: 'Provider default' },
      { value: 'standard', label: 'Standard' },
      { value: 'infrequent_access', label: 'Infrequent Access' },
    ]
  }

  return null
}

const parseIntervalSeconds = (value?: number | string): number => {
  if (typeof value === 'number' && Number.isFinite(value) && value > 0) return value
  if (typeof value !== 'string') return DEFAULT_SYNC_POLICY.interval

  const trimmed = value.trim()
  if (!trimmed) return DEFAULT_SYNC_POLICY.interval
  if (/^\d+$/.test(trimmed)) return Number(trimmed)

  const matches = [...trimmed.matchAll(/(\d+)\s*([dhms])/gi)]
  if (!matches.length) return DEFAULT_SYNC_POLICY.interval

  const seconds = matches.reduce((sum, [, amount, unit]) => {
    const n = Number(amount)
    if (unit.toLowerCase() === 'd') return sum + n * 86400
    if (unit.toLowerCase() === 'h') return sum + n * 3600
    if (unit.toLowerCase() === 'm') return sum + n * 60
    return sum + n
  }, 0)

  return seconds > 0 ? seconds : DEFAULT_SYNC_POLICY.interval
}

const normalizeNumber = (value: unknown, fallback: number): number => {
  const n = typeof value === 'number' ? value : Number(value)
  return Number.isFinite(n) && n > 0 ? n : fallback
}

const nullableNumber = (value: unknown): number | null => {
  if (value === '' || value == null) return null
  const n = typeof value === 'number' ? value : Number(value)
  return Number.isFinite(n) && n >= 0 ? n : null
}

const normalizeBudget = (budget?: Partial<S3RequestBudget>): S3RequestBudget => ({
  list_requests: budget?.list_requests ?? null,
  head_requests: budget?.head_requests ?? null,
  get_requests: budget?.get_requests ?? null,
  put_requests: budget?.put_requests ?? null,
  copy_requests: budget?.copy_requests ?? null,
  delete_requests: budget?.delete_requests ?? null,
  downloaded_bytes: budget?.downloaded_bytes ?? null,
})

const budgetsEqual = (a: S3RequestBudget, b: S3RequestBudget) =>
  BUDGET_FIELDS.every(({ key }) => (a[key] ?? null) === (b[key] ?? null))

const budgetPresetFor = (budget: S3RequestBudget): S3BudgetPreset => {
  for (const preset of ['conservative', 'balanced', 'bulk', 'unlimited'] as const) {
    if (budgetsEqual(budget, S3_BUDGET_PRESETS[preset])) return preset
  }
  return 'custom'
}

const formatBudgetValue = (value: number | null) => (value == null ? 'Unlimited' : value.toLocaleString())

const syncDefaults = (sync?: Partial<RemoteSyncPolicy>): FormSyncPolicy => {
  const budget =
    sync?.s3_request_budget ? normalizeBudget(sync.s3_request_budget) : { ...DEFAULT_SYNC_POLICY.s3_request_budget }
  return {
    id: sync?.id,
    vault_id: sync?.vault_id,
    strategy: sync?.strategy ?? DEFAULT_SYNC_POLICY.strategy,
    conflict_policy: sync?.conflict_policy ?? DEFAULT_SYNC_POLICY.conflict_policy,
    interval: parseIntervalSeconds(sync?.interval),
    enabled: sync?.enabled ?? DEFAULT_SYNC_POLICY.enabled,
    s3_request_budget: budgetPresetFor(budget) === 'custom' ? budget : { ...budget },
    max_remote_index_age_seconds:
      sync?.max_remote_index_age_seconds === null ?
        null
      : normalizeNumber(sync?.max_remote_index_age_seconds, DEFAULT_SYNC_POLICY.max_remote_index_age_seconds ?? 86400),
  }
}

const defaultValuesFor = (initialValues?: Partial<LocalDiskVault | S3Vault | Vault>): VaultFormValues => {
  const type = initialValues?.type ?? 'local'
  const s3 = type === 's3' ? new S3Vault(initialValues as Partial<S3Vault>) : null
  const sync = syncDefaults(s3?.sync)
  const mountPoint = initialValues && 'mount_point' in initialValues ? String(initialValues.mount_point ?? '') : ''

  return {
    name: initialValues?.name ?? '',
    type,
    mount_point: mountPoint,
    api_key_id: s3?.api_key_id || undefined,
    bucket: s3?.bucket ?? '',
    storage_tier_id: s3?.storage_tier_id ?? null,
    encrypt_upstream: s3?.encrypt_upstream ?? true,
    budget_preset: budgetPresetFor(sync.s3_request_budget),
    sync,
  }
}

const buildSyncPayload = (
  values: VaultFormValues,
  initialValues?: Partial<LocalDiskVault | S3Vault | Vault>,
): RemoteSyncPolicy => {
  const initialSync = initialValues?.type === 's3' ? new S3Vault(initialValues as Partial<S3Vault>).sync : undefined
  const sync = values.sync ?? syncDefaults(initialSync)
  const preset = values.budget_preset ?? budgetPresetFor(sync.s3_request_budget)
  const budget =
    preset === 'custom' ?
      normalizeBudget(sync.s3_request_budget)
    : { ...S3_BUDGET_PRESETS[preset as S3PresetWithoutCustom] }

  return {
    id: initialSync?.id,
    vault_id: initialSync?.vault_id,
    strategy: sync.strategy ?? DEFAULT_SYNC_POLICY.strategy,
    conflict_policy: sync.conflict_policy ?? DEFAULT_SYNC_POLICY.conflict_policy,
    interval: normalizeNumber(sync.interval, DEFAULT_SYNC_POLICY.interval),
    enabled: sync.enabled ?? DEFAULT_SYNC_POLICY.enabled,
    s3_request_budget: budget,
    max_remote_index_age_seconds:
      sync.max_remote_index_age_seconds === null ?
        null
      : normalizeNumber(sync.max_remote_index_age_seconds, DEFAULT_SYNC_POLICY.max_remote_index_age_seconds ?? 86400),
  }
}

const BudgetInput = ({
  name,
  label,
  register,
}: {
  name: BudgetKey
  label: string
  register: UseFormRegister<VaultFormValues>
}) => (
  <div>
    <label className="block text-xs font-medium text-white/70">{label}</label>
    <input
      type="number"
      min={0}
      placeholder="Unlimited"
      {...register(`sync.s3_request_budget.${name}`, { setValueAs: nullableNumber })}
      className="mt-1 w-full rounded border p-2"
    />
  </div>
)

const BudgetPreview = ({ budget }: { budget: S3RequestBudget }) => (
  <div className="grid gap-2 sm:grid-cols-2 lg:grid-cols-3">
    {BUDGET_FIELDS.map(field => (
      <div key={field.key} className="rounded border border-white/10 bg-white/[0.03] px-3 py-2 text-left">
        <div className="text-[11px] font-medium text-white/45 uppercase">{field.label}</div>
        <div className="mt-1 text-sm text-white/85">{formatBudgetValue(budget[field.key])}</div>
      </div>
    ))}
  </div>
)

const S3Guardrails = ({
  control,
  register,
  setValue,
}: {
  control: Control<VaultFormValues>
  register: UseFormRegister<VaultFormValues>
  setValue: UseFormSetValue<VaultFormValues>
}) => {
  const budgetPreset = useWatch({ control, name: 'budget_preset' }) ?? 'balanced'
  const previewBudget = budgetPreset === 'custom' ? null : S3_BUDGET_PRESETS[budgetPreset]

  useEffect(() => {
    if (budgetPreset === 'custom') return
    const budget = S3_BUDGET_PRESETS[budgetPreset]
    setValue('sync.s3_request_budget.list_requests', budget.list_requests)
    setValue('sync.s3_request_budget.head_requests', budget.head_requests)
    setValue('sync.s3_request_budget.get_requests', budget.get_requests)
    setValue('sync.s3_request_budget.put_requests', budget.put_requests)
    setValue('sync.s3_request_budget.copy_requests', budget.copy_requests)
    setValue('sync.s3_request_budget.delete_requests', budget.delete_requests)
    setValue('sync.s3_request_budget.downloaded_bytes', budget.downloaded_bytes)
  }, [budgetPreset, setValue])

  return (
    <section id="s3-guardrails" className="border-t border-white/10 pt-4 text-left">
      <div className="flex flex-col gap-1">
        <h2 className="text-base font-semibold">Cost Controls</h2>
        <p className="text-sm text-white/50">S3 request and index guardrails for this vault.</p>
      </div>

      <div className="mt-4 grid gap-4 md:grid-cols-2">
        <div>
          <label className="block text-sm font-medium">Budget preset</label>
          <select {...register('budget_preset')} className="mt-1 w-full rounded border p-2">
            <option value="conservative">Conservative</option>
            <option value="balanced">Balanced</option>
            <option value="bulk">Bulk</option>
            <option value="unlimited">Unlimited</option>
            <option value="custom">Custom</option>
          </select>
        </div>

        <div>
          <label className="block text-sm font-medium">Sync interval (seconds)</label>
          <input
            type="number"
            min={1}
            {...register('sync.interval', {
              setValueAs: value => normalizeNumber(value, DEFAULT_SYNC_POLICY.interval),
            })}
            className="mt-1 w-full rounded border p-2"
          />
        </div>

        <div>
          <label className="block text-sm font-medium">Max remote index age (seconds)</label>
          <input
            type="number"
            min={1}
            placeholder="Unlimited"
            {...register('sync.max_remote_index_age_seconds', { setValueAs: nullableNumber })}
            className="mt-1 w-full rounded border p-2"
          />
        </div>

        <div>
          <label className="block text-sm font-medium">Strategy</label>
          <select {...register('sync.strategy')} className="mt-1 w-full rounded border p-2">
            <option value="cache">Cache</option>
            <option value="sync">Sync</option>
            <option value="mirror">Mirror</option>
          </select>
        </div>

        <div>
          <label className="block text-sm font-medium">Conflict policy</label>
          <select {...register('sync.conflict_policy')} className="mt-1 w-full rounded border p-2">
            <option value="keep_local">Keep local</option>
            <option value="keep_remote">Keep remote</option>
            <option value="keep_newest">Keep newest</option>
            <option value="ask">Ask</option>
          </select>
        </div>

        <label className="flex items-center gap-2 self-end text-sm font-medium">
          <input type="checkbox" {...register('sync.enabled')} className="h-4 w-4" />
          Sync enabled
        </label>
      </div>

      <div className="mt-4">
        <div className="mb-2 text-sm font-medium">Request limits</div>
        {budgetPreset === 'custom' ?
          <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
            {BUDGET_FIELDS.map(field => (
              <BudgetInput key={field.key} name={field.key} label={field.label} register={register} />
            ))}
          </div>
        : <details className="rounded border border-white/10 bg-white/[0.03] p-3">
            <summary className="cursor-pointer text-sm text-white/75">Preset request limits</summary>
            <div className="mt-3">{previewBudget && <BudgetPreview budget={previewBudget} />}</div>
          </details>
        }
      </div>
    </section>
  )
}

const VaultForm = ({ initialValues }: { initialValues?: Partial<LocalDiskVault | S3Vault | Vault> }) => {
  const [isSubmitting, setIsSubmitting] = useState(false)
  const router = useRouter()
  const apiKeys = useApiKeyStore(state => state.apiKeys)
  const addVault = useVaultStore(state => state.addVault)
  const updateVault = useVaultStore(state => state.updateVault)

  const defaults = useMemo(() => defaultValuesFor(initialValues), [initialValues])

  const {
    register,
    handleSubmit,
    control,
    setValue,
    formState: { errors },
  } = useForm<VaultFormValues>({ defaultValues: defaults })

  const type = useWatch({ control, name: 'type' }) ?? 'local'
  const selectedApiKeyId = useWatch({ control, name: 'api_key_id' })
  const selectedApiKey = apiKeys.find(k => k.api_key_id === Number(selectedApiKeyId))
  const storageTierOptions = storageTierOptionsForProvider(selectedApiKey?.provider)
  const providerKnown = Boolean(selectedApiKey?.provider)

  useEffect(() => {
    if (type === 's3' && providerKnown && !storageTierOptions) setValue('storage_tier_id', null)
  }, [providerKnown, setValue, storageTierOptions, type])

  const onSubmit = async (data: VaultFormValues) => {
    setIsSubmitting(true)
    try {
      const isEdit = Boolean(initialValues?.name || initialValues?.id)

      if (isEdit) {
        const updated =
          data.type === 'local' ?
            await updateVault(
              new LocalDiskVault({
                ...initialValues,
                name: data.name,
                type: 'local',
                mount_point: data.mount_point ?? '',
              }),
            )
          : await updateVault(
              new S3Vault({
                ...initialValues,
                name: data.name,
                type: 's3',
                api_key_id: Number(data.api_key_id),
                bucket: data.bucket ?? '',
                storage_tier_id: data.storage_tier_id ?? null,
                encrypt_upstream: data.encrypt_upstream ?? true,
                sync: buildSyncPayload(data, initialValues),
              }),
            )

        router.push(`/vaults/${updated.id}`)
        return
      }

      if (data.type === 'local') {
        await addVault({ name: data.name, type: 'local', mount_point: data.mount_point ?? '' })
      } else {
        await addVault({
          name: data.name,
          type: 's3',
          api_key_id: Number(data.api_key_id),
          bucket: data.bucket ?? '',
          storage_tier_id: data.storage_tier_id ?? null,
          encrypt_upstream: data.encrypt_upstream ?? true,
          sync: buildSyncPayload(data, initialValues),
        })
      }

      router.push('/dashboard/vaults')
    } finally {
      setIsSubmitting(false)
    }
  }

  const LocalInputs = () =>
    type === 'local' && (
      <motion.div key="local" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
        <label className="block text-sm font-medium">Mount Point</label>
        <input
          {...register('mount_point', { required: 'Mount point is required' })}
          className="mt-1 w-full rounded border p-2"
        />
        {'mount_point' in errors && errors.mount_point && (
          <span className="text-sm text-red-400">{errors.mount_point.message}</span>
        )}
      </motion.div>
    )

  const S3Inputs = () =>
    type === 's3' && (
      <motion.div
        key="s3"
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        exit={{ opacity: 0 }}
        className="space-y-4">
        <div>
          <label className="block text-sm font-medium">API Key</label>
          <Controller
            name="api_key_id"
            control={control}
            rules={{ required: 'API Key is required' }}
            render={({ field }) => (
              <select
                {...field}
                onChange={e => field.onChange(e.target.value ? Number(e.target.value) : undefined)}
                value={field.value ?? ''}
                className="mt-1 w-full rounded border p-2">
                <option value="">Select API Key</option>
                {apiKeys.map(k => (
                  <option key={k.api_key_id} value={k.api_key_id}>
                    {k.name}
                  </option>
                ))}
              </select>
            )}
          />
          {'api_key_id' in errors && errors.api_key_id && (
            <span className="text-sm text-red-400">{errors.api_key_id.message}</span>
          )}
        </div>

        <div>
          <label className="block text-sm font-medium">Bucket</label>
          <input
            {...register('bucket', { required: 'Bucket is required' })}
            className="mt-1 w-full rounded border p-2"
          />
          {'bucket' in errors && errors.bucket && <span className="text-sm text-red-400">{errors.bucket.message}</span>}
        </div>

        <div>
          <label className="block text-sm font-medium">Storage Tier</label>
          {storageTierOptions ?
            <Controller
              name="storage_tier_id"
              control={control}
              render={({ field }) => (
                <select
                  {...field}
                  value={field.value ?? ''}
                  onChange={e => field.onChange(e.target.value || null)}
                  className="mt-1 w-full rounded border p-2">
                  {storageTierOptions.map(option => (
                    <option key={option.value || 'default'} value={option.value}>
                      {option.label}
                    </option>
                  ))}
                </select>
              )}
            />
          : providerKnown ?
            <input value="Provider default" readOnly className="mt-1 w-full rounded border p-2 opacity-70" />
          : <input
              {...register('storage_tier_id')}
              placeholder="Provider default"
              className="mt-1 w-full rounded border p-2"
            />
          }
        </div>

        <label className="flex items-center gap-2 text-sm font-medium">
          <input type="checkbox" {...register('encrypt_upstream')} className="h-4 w-4" />
          Encrypt upstream objects
        </label>

        <S3Guardrails control={control} register={register} setValue={setValue} />
      </motion.div>
    )

  if (isSubmitting)
    return (
      <div className="text-primary flex h-screen w-full flex-col items-center justify-center space-y-4">
        <VaultIcon className="text-primary h-20 w-20 animate-pulse fill-current" />
        <p className="text-xl font-semibold tracking-wide">Saving vault...</p>
        <p className="text-primary text-sm">This should only take a moment.</p>
      </div>
    )

  return (
    <motion.form
      onSubmit={handleSubmit(onSubmit)}
      initial={{ opacity: 0, y: 10 }}
      animate={{ opacity: 1, y: 0 }}
      className="flex h-fit flex-col gap-4 rounded-lg border p-4 shadow">
      <div>
        <label className="block text-sm font-medium">Vault Name</label>
        <input {...register('name', { required: 'Name is required' })} className="mt-1 w-full rounded border p-2" />
        {errors.name && <span className="text-sm text-red-400">{errors.name.message}</span>}
      </div>

      <div>
        <label className="block text-sm font-medium">Type</label>
        <select {...register('type')} className="mt-1 w-full rounded border p-2">
          <option value="local">Local</option>
          <option value="s3">S3</option>
        </select>
      </div>

      <LocalInputs />
      <S3Inputs />

      <Button type="submit" className="mt-2">
        {initialValues?.name ? 'Update Vault' : 'Add Vault'}
      </Button>
    </motion.form>
  )
}

export default VaultForm
