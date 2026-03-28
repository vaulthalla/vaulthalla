'use client'

import { useEffect, useState } from 'react'
import { useForm } from 'react-hook-form'
import { useApiKeyStore } from '@/stores/apiKeyStore'
import { useAuthStore } from '@/stores/authStore'
import { redirect } from 'next/navigation'
import { S3APIKey } from '@/models/apiKey'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { Button } from '@/components/Button'

type FormData = {
  name: string
  provider: string
  access_key: string
  secret_access_key: string
  region: string
  endpoint: string
}

const S3APIKeyForm = ({ edit, id }: { edit?: boolean; id?: number }) => {
  const [loading, setLoading] = useState(edit ?? false)

  const {
    register,
    handleSubmit,
    setValue,
    formState: { errors },
  } = useForm<FormData>()

  const { getApiKey, removeApiKey, addApiKey } = useApiKeyStore()
  const user_id = useAuthStore.getState().user?.id

  useEffect(() => {
    if (edit && id) {
      ;(async () => {
        try {
          const key = (await getApiKey({ id })) as S3APIKey
          setValue('name', key.name)
          setValue('provider', key.provider)
          setValue('access_key', key.access_key)
          setValue('secret_access_key', key.secret_access_key)
          setValue('region', key.region)
          setValue('endpoint', key.endpoint)
        } catch (err) {
          console.error('[Edit API Key] Failed to load key:', err)
        } finally {
          setLoading(false) // flip off loader
        }
      })()
    }
  }, [edit, id, getApiKey, setValue])

  const onSubmit = async (data: FormData) => {
    if (!user_id) {
      console.error('User ID is not available')
      return
    }

    const payload = { ...data, user_id, type: 's3' }

    if (edit && id) {
      await removeApiKey({ id })
    }

    await addApiKey(payload)
    redirect('/dashboard/api-keys')
  }

  const onDelete = async () => {
    if (!id) {
      console.error('No API key ID provided for deletion')
      return
    }

    await removeApiKey({ id })
    redirect('/dashboard/api-keys')
  }

  const inputStyles = 'border border-gray-300 rounded-md p-2 w-full focus:outline-none focus:ring-2 focus:ring-cyan-500'

  const renderError = (error: string | undefined) =>
    typeof error === 'string' ? <span className="text-sm text-red-500">{error}</span> : null

  const providers = [
    'AWS',
    'Cloudflare R2',
    'Wasabi',
    'Backblaze B2',
    'DigitalOcean',
    'MinIO',
    'Ceph',
    'Storj',
    'Other',
  ]

  if (loading) return <CircleNotchLoader />

  return (
    <div className="flex h-screen flex-col items-center justify-center space-y-2">
      <h1 className="mb-12 text-4xl font-semibold">
        {edit ? 'Edit S3-Compatible API Key' : 'Add S3-Compatible API Key'}
      </h1>

      <form onSubmit={handleSubmit(onSubmit)} className="flex w-full max-w-md flex-col space-y-4">
        <div>
          <label htmlFor="name">Name:</label>
          <input id="name" className={inputStyles} {...register('name', { required: 'Name is required' })} />
          {renderError(errors.name?.message)}
        </div>

        <div>
          <label htmlFor="provider">Provider:</label>
          <select id="provider" className={inputStyles} {...register('provider', { required: 'Provider is required' })}>
            <option value="">-- Select Provider --</option>
            {providers.map(p => (
              <option key={p} value={p}>
                {p}
              </option>
            ))}
          </select>
          {renderError(errors.provider?.message)}
        </div>

        <div>
          <label htmlFor="access_key">Access Key:</label>
          <input
            id="access_key"
            className={inputStyles}
            {...register('access_key', { required: 'Access key is required' })}
          />
          {renderError(errors.access_key?.message)}
        </div>

        <div>
          <label htmlFor="secret_access_key">Secret Access Key:</label>
          <input
            id="secret_access_key"
            type="password"
            className={inputStyles}
            {...register('secret_access_key', { required: 'Secret access key is required' })}
          />
          {renderError(errors.secret_access_key?.message)}
        </div>

        <div>
          <label htmlFor="region">Region:</label>
          <input id="region" className={inputStyles} {...register('region', { required: 'Region is required' })} />
          {renderError(errors.region?.message)}
        </div>

        <div>
          <label htmlFor="endpoint">Endpoint:</label>
          <input
            id="endpoint"
            className={inputStyles}
            {...register('endpoint', { required: 'Endpoint is required' })}
          />
          {renderError(errors.endpoint?.message)}
        </div>

        <button
          type="submit"
          className="transform cursor-pointer rounded-lg bg-cyan-700 py-2.5 font-semibold text-white transition-transform duration-200 hover:scale-105 hover:bg-cyan-800 disabled:cursor-not-allowed disabled:opacity-50">
          {edit ? 'Update API Key' : 'Add API Key'}
        </button>

        {edit && id && (
          <Button variant="destructive" type="button" className="mt-4" onClick={onDelete}>
            Delete API Key
          </Button>
        )}
      </form>
    </div>
  )
}

export default S3APIKeyForm
