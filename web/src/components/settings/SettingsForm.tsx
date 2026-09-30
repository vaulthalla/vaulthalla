'use client'

import { useForm, SubmitHandler, FieldValues, get } from 'react-hook-form'
import * as motion from 'motion/react-client'
import { JSX, useEffect, useState } from 'react'
import { Settings, SettingsSection, SettingsValue, isSettingsSection } from '@/models/settings'
import { useSettingsStore } from '@/stores/settingsStore'
import { Button } from '@/components/Button'

const sectionVariants = { hidden: { opacity: 0, y: 10 }, visible: { opacity: 1, y: 0 } }

const transformDisplayName = (name: string) =>
  name
    .replace(/_/g, ' ')
    .replace(/\b\w/g, char => char.toUpperCase())
    .replace(/([a-z])([A-Z])/g, '$1 $2')
    .replace('Ip', 'IP')
    .replace('Jwt', 'JWT')
    .replace('Mb', 'MB')

export default function SettingsForm(settings: Settings) {
  const {
    register,
    handleSubmit,
    formState: { errors },
    reset,
  } = useForm<FieldValues>({ defaultValues: settings as unknown as FieldValues })
  const [status, setStatus] = useState<{ ok: boolean; message: string } | null>(null)

  useEffect(() => {
    reset(settings as unknown as FieldValues)
  }, [settings, reset])

  // Unrendered values (arrays, nulls) stay in the form state from defaultValues and are sent back unchanged;
  // the server merges the payload onto the current config either way.
  const submit: SubmitHandler<FieldValues> = async data => {
    try {
      const saved = await useSettingsStore.getState().updateSettings(data as Partial<Settings>)
      reset(saved as unknown as FieldValues)
      setStatus({ ok: true, message: 'Settings saved.' })
    } catch (err) {
      setStatus({ ok: false, message: err instanceof Error ? err.message : 'Failed to save settings.' })
    }
  }

  const renderField = (label: string, path: string, value: SettingsValue): JSX.Element | null => {
    const displayLabel = transformDisplayName(label)
    const error = get(errors, path)

    if (typeof value === 'boolean') {
      return (
        <div key={path} className="flex items-center gap-2">
          <label className="flex items-center space-x-2 text-sm font-medium">
            <input type="checkbox" className="form-checkbox" {...register(path)} />
            <span>{displayLabel}</span>
          </label>
        </div>
      )
    }

    if (typeof value === 'number' || typeof value === 'string') {
      const isNumber = typeof value === 'number'
      return (
        <div key={path} className="space-y-1">
          <label className="text-sm font-semibold">{displayLabel}</label>
          <input
            type={isNumber ? 'number' : 'text'}
            className="w-full rounded border px-3 py-2 dark:bg-gray-700"
            {...register(
              path,
              isNumber ?
                { valueAsNumber: true, required: `${displayLabel} is required` }
              : { required: `${displayLabel} is required` },
            )}
          />
          {error && <p className="text-sm text-red-500">{String(error.message ?? 'Invalid value')}</p>}
        </div>
      )
    }

    // null (optional/unset) and list values are shown read-only and preserved as-is.
    return (
      <div key={path} className="space-y-1">
        <label className="text-sm font-semibold">{displayLabel}</label>
        <code className="block w-full rounded border px-3 py-2 text-xs text-gray-400">{JSON.stringify(value)}</code>
      </div>
    )
  }

  const renderSection = (section: SettingsSection, pathPrefix: string): JSX.Element[] => {
    const fields: JSX.Element[] = []
    const nested: JSX.Element[] = []

    for (const [key, value] of Object.entries(section)) {
      const path = `${pathPrefix}.${key}`
      if (isSettingsSection(value)) {
        nested.push(
          <fieldset key={path} className="col-span-full rounded border p-3">
            <legend className="px-1 text-sm font-bold">{transformDisplayName(key)}</legend>
            <div className="grid grid-cols-1 gap-4 md:grid-cols-2">{renderSection(value, path)}</div>
          </fieldset>,
        )
        continue
      }
      const field = renderField(key, path, value)
      if (field) fields.push(field)
    }

    return [...fields, ...nested]
  }

  return (
    <form onSubmit={handleSubmit(submit)} className="space-y-6">
      {Object.entries(settings).map(([sectionKey, sectionValue]) => {
        if (!isSettingsSection(sectionValue as SettingsValue)) return null
        return (
          <motion.div
            key={sectionKey}
            variants={sectionVariants}
            initial="hidden"
            animate="visible"
            transition={{ duration: 0.2, delay: 0.1 }}
            className="rounded-xl border p-4 shadow">
            <h3 className="mb-4 text-lg font-bold capitalize">{sectionKey.replace(/_/g, ' ')}</h3>
            <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
              {renderSection(sectionValue as SettingsSection, sectionKey)}
            </div>
          </motion.div>
        )
      })}
      {status && <p className={status.ok ? 'text-sm text-green-500' : 'text-sm text-red-500'}>{status.message}</p>}
      <div className="text-right">
        <Button type="submit">Save Settings</Button>
      </div>
    </form>
  )
}
