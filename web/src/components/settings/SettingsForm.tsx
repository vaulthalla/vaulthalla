'use client'

import { useForm, SubmitHandler, Path, get } from 'react-hook-form'
import * as motion from 'motion/react-client'
import { JSX, useEffect } from 'react'
import { Settings } from '@/models/settings'
import { useSettingsStore } from '@/stores/settingsStore'
import { Button } from '@/components/Button'

const sectionVariants = { hidden: { opacity: 0, y: 10 }, visible: { opacity: 1, y: 0 } }

export default function SettingsForm(settings: Settings) {
  const {
    register,
    handleSubmit,
    formState: { errors },
    reset,
  } = useForm<Settings>({ defaultValues: settings })

  useEffect(() => {
    reset(settings)
  }, [settings, reset])

  const submit: SubmitHandler<Settings> = data => {
    useSettingsStore.getState().updateSettings(data)
  }

  const transformDisplayName = (name: string) => {
    return name
      .replace(/_/g, ' ')
      .replace(/\b\w/g, char => char.toUpperCase())
      .replace(/([a-z])([A-Z])/g, '$1 $2')
      .replace('Ip', 'IP')
      .replace('Jwt', 'JWT')
      .replace('Mb', 'MB')
  }

  const renderInput = (label: string, path: Path<Settings>, type: string = 'text') => {
    const displayLabel = transformDisplayName(label)

    const error = get(errors, path) // 👈 handles "server.host" style paths safely

    if (type === 'checkbox') {
      return (
        <div key={path} className="flex items-center gap-2">
          <label className="flex items-center space-x-2 text-sm font-medium">
            <input type="checkbox" className="form-checkbox" {...register(path)} />
            <span>{displayLabel}</span>
          </label>
          {error && <p className="text-sm text-red-500">{String(error.message ?? 'Invalid value')}</p>}
        </div>
      )
    }

    return (
      <div key={path} className="space-y-1">
        <label className="text-sm font-semibold">{displayLabel}</label>
        <input
          type={type}
          className="w-full rounded border px-3 py-2 dark:bg-gray-700"
          {...register(path, { required: `${displayLabel} is required` })}
        />
        {error && <p className="text-sm text-red-500">{String(error.message ?? 'Invalid value')}</p>}
      </div>
    )
  }

  return (
    <form onSubmit={handleSubmit(submit)} className="space-y-6">
      {Object.entries(settings).map(([sectionKey, sectionValue]) => {
        const entries = Object.entries(sectionValue)

        // Split and sort fields by input type
        const [textAndNumbers, checkboxes] = entries.reduce(
          ([textFields, boolFields], [key, val]) => {
            const type =
              typeof val === 'boolean' ? 'checkbox'
              : typeof val === 'number' ? 'number'
              : 'text'
            const path = `${sectionKey}.${key}` as Path<Settings>
            const entry = renderInput(key, path, type)
            return type === 'checkbox' ? [textFields, [...boolFields, entry]] : [[...textFields, entry], boolFields]
          },
          [[], []] as [JSX.Element[], JSX.Element[]],
        )

        return (
          <motion.div
            key={sectionKey}
            variants={sectionVariants}
            initial="hidden"
            animate="visible"
            transition={{ duration: 0.2, delay: 0.1 }}
            className="rounded-xl border p-4 shadow">
            <h3 className="mb-4 text-lg font-bold capitalize">{sectionKey.replace('_', ' ')}</h3>
            <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
              {textAndNumbers}
              {checkboxes}
            </div>
          </motion.div>
        )
      })}
      <div className="text-right">
        <Button type="submit">Save Settings</Button>
      </div>
    </form>
  )
}
