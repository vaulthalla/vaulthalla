'use client'

import { useApiKeyStore } from '@/stores/apiKeyStore'
import { useVaultStore } from '@/stores/vaultStore'
import { useForm, Controller, useWatch } from 'react-hook-form'
import * as motion from 'motion/react-client'
import { Button } from '@/components/Button'
import { LocalDiskVault, S3Vault } from '@/models/vaults'
import { useRouter } from 'next/navigation'
import { useEffect, useState } from 'react'
import VaultIcon from '@/fa-duotone/vault.svg'

type VaultFormValues =
  | ({ type: 'local' } & Pick<LocalDiskVault, 'name' | 'mount_point'>)
  | ({ type: 's3' } & Pick<S3Vault, 'name' | 'api_key_id' | 'bucket' | 'storage_tier_id'>)

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

const VaultForm = ({ initialValues }: { initialValues?: Partial<LocalDiskVault | S3Vault> }) => {
  const [isSubmitting, setIsSubmitting] = useState(false)
  const router = useRouter()
  const apiKeys = useApiKeyStore(state => state.apiKeys)
  const addVault = useVaultStore(state => state.addVault)
  const updateVault = useVaultStore(state => state.updateVault)

  const {
    register,
    handleSubmit,
    control,
    setValue,
    formState: { errors },
  } = useForm<VaultFormValues>({ defaultValues: initialValues || { name: '', type: 'local', mount_point: '' } })

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
    if (initialValues?.name || initialValues?.id) {
      let vault: LocalDiskVault | S3Vault
      if (type === 'local') vault = new LocalDiskVault({ ...initialValues, ...data })
      else vault = new S3Vault({ ...initialValues, ...data })
      await updateVault(vault)
    } else await addVault(data)
    router.push('/dashboard/vaults')
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
      <motion.div key="s3" initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}>
        <label className="block text-sm font-medium">API Key</label>
        <Controller
          name="api_key_id"
          control={control}
          rules={{ required: 'API Key is required' }}
          render={({ field }) => (
            <select
              {...field}
              onChange={e => field.onChange(Number(e.target.value))} // 🔧 force number
              value={field.value ?? ''} // Ensure controlled
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

        <label className="mt-2 block text-sm font-medium">Bucket</label>
        <input {...register('bucket', { required: 'Bucket is required' })} className="mt-1 w-full rounded border p-2" />
        {'bucket' in errors && errors.bucket && <span className="text-sm text-red-400">{errors.bucket.message}</span>}

        <label className="mt-2 block text-sm font-medium">Storage Tier</label>
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
      </motion.div>
    )

  if (isSubmitting)
    return (
      <div className="text-primary flex h-screen w-full flex-col items-center justify-center space-y-4">
        <VaultIcon className="text-primary h-20 w-20 animate-pulse fill-current" />
        <p className="text-xl font-semibold tracking-wide">Preparing your new Vault...</p>
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
