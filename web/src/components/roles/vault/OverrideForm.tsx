'use client'

import { useEffect, useMemo, useState } from 'react'
import * as motion from 'motion/react-client'
import { useForm, useWatch } from 'react-hook-form'
import { Button } from '@/components/Button'
import { Permission } from '@/models/role'
import { usePermStore } from '@/stores/usePermStore'

const sectionVariants = { hidden: { opacity: 0, y: 10 }, visible: { opacity: 1, y: 0 } }

export type PermissionOverrideEffect = 'allow' | 'deny'

export type PermissionOverrideFormData = {
  id?: number
  permission_qualified: string
  enabled: boolean
  effect: PermissionOverrideEffect
  pattern: string
}

type PermissionOverrideFormProps = {
  defaultValues?: Partial<PermissionOverrideFormData>
  title?: string
  subtitle?: string
  onSubmitAction?: (data: PermissionOverrideFormData) => Promise<void> | void
  onCancelAction?: () => void
}

function toTitleCase(value: string) {
  return value.charAt(0).toUpperCase() + value.slice(1)
}

function formatQualifiedPermissionLabel(permission: Permission) {
  const trimmed = permission.qualified.replace(/^vault\.fs\.?/, '')
  const parts = trimmed.split('.')
  const leaf = parts.pop() ?? permission.slug
  const group = parts.join(' / ')

  return {
    value: permission.qualified,
    label: toTitleCase(leaf.replaceAll('_', ' ')),
    meta: group || 'filesystem',
    description: permission.description,
  }
}

function isLikelyValidGlobPattern(pattern: string) {
  if (!pattern.trim()) return true
  return pattern.startsWith('/')
}

export default function VaultPermissionOverrideForm({
  defaultValues,
  title = 'Permission Override',
  subtitle = 'Override a vault filesystem permission and optionally constrain it to a vault-relative path glob.',
  onSubmitAction,
  onCancelAction,
}: PermissionOverrideFormProps) {
  const { permissions, fetchPermissions } = usePermStore()

  const [isInitializing, setIsInitializing] = useState(true)
  const [isSubmitting, setIsSubmitting] = useState(false)
  const [permissionSearch, setPermissionSearch] = useState('')
  const [isPermissionOpen, setIsPermissionOpen] = useState(false)

  const initialFormValues = useMemo<PermissionOverrideFormData>(() => ({
    id: defaultValues?.id,
    permission_qualified: defaultValues?.permission_qualified ?? '',
    enabled: defaultValues?.enabled ?? true,
    effect: defaultValues?.effect ?? 'allow',
    pattern: defaultValues?.pattern ?? '',
  }), [
    defaultValues?.id,
    defaultValues?.permission_qualified,
    defaultValues?.enabled,
    defaultValues?.effect,
    defaultValues?.pattern,
  ])

  const {
    register,
    handleSubmit,
    control,
    setValue,
    reset,
    formState: { errors },
  } = useForm<PermissionOverrideFormData>({
    defaultValues: initialFormValues,
    mode: 'onChange',
  })

  const permissionQualified = useWatch({ control, name: 'permission_qualified' }) ?? ''
  const enabled = useWatch({ control, name: 'enabled' }) ?? true
  const effect = useWatch({ control, name: 'effect' }) ?? 'allow'
  const isEditing = Boolean(initialFormValues.id)

  useEffect(() => {
    reset(initialFormValues)
  }, [initialFormValues, reset])

  useEffect(() => {
    let mounted = true

    const init = async () => {
      setIsInitializing(true)
      if (!permissions.length) await fetchPermissions()
      if (!mounted) return
      setIsInitializing(false)
    }

    void init()
    return () => {
      mounted = false
    }
  }, [permissions.length, fetchPermissions])

  const vaultFsPermissions = useMemo(
    () => permissions.filter(permission => permission.qualified.startsWith('vault.fs')),
    [permissions],
  )

  const permissionOptions = useMemo(() => vaultFsPermissions.map(formatQualifiedPermissionLabel), [vaultFsPermissions])

  const selectedPermission = useMemo(
    () => permissionOptions.find(option => option.value === permissionQualified) ?? null,
    [permissionOptions, permissionQualified],
  )

  const filteredPermissionOptions = useMemo(() => {
    const query = permissionSearch.trim().toLowerCase()
    if (!query) return permissionOptions

    return permissionOptions.filter(
      option =>
        option.value.toLowerCase().includes(query)
        || option.label.toLowerCase().includes(query)
        || option.meta.toLowerCase().includes(query)
        || option.description.toLowerCase().includes(query),
    )
  }, [permissionOptions, permissionSearch])

  const patternError = errors.pattern?.message as string | undefined

  const canSubmit = !!permissionQualified && !patternError

  const handleReset = () => {
    setPermissionSearch('')
    setIsPermissionOpen(false)
    reset(initialFormValues)
  }

  const onSubmit = async (data: PermissionOverrideFormData) => {
    setIsSubmitting(true)
    try {
      await onSubmitAction?.({ ...data, pattern: data.pattern.trim() })
    } finally {
      setIsSubmitting(false)
    }
  }

  if (isInitializing)
    return (
      <div className="rounded-3xl border border-white/10 bg-black/20 p-6 text-white/70">
        Loading permission override form...
      </div>
    )

  return (
    <motion.div variants={sectionVariants} initial="hidden" animate="visible" transition={{ duration: 0.2 }}>
      <div className="rounded-3xl border border-white/10 bg-white/5 p-6 shadow">
        <div className="mb-6 flex items-start justify-between gap-4">
          <div>
            <div className="mb-2">
              <span className="inline-flex rounded-full border border-cyan-400/20 bg-cyan-500/10 px-2.5 py-1 text-[10px] font-bold tracking-[0.18em] text-cyan-200 uppercase">
                {isEditing ? 'Edit override' : 'Create override'}
              </span>
            </div>

            <h2 className="text-2xl font-semibold text-white">{title}</h2>
            <p className="mt-2 max-w-3xl text-sm text-white/60">{subtitle}</p>
          </div>

          <div className="rounded-2xl border border-white/8 bg-black/20 px-4 py-3 text-right">
            <div className="text-[10px] font-bold tracking-[0.18em] text-white/35 uppercase">Effect</div>
            <div className="mt-1 text-lg font-semibold text-white">
              {enabled ?
                effect === 'allow' ?
                  <span className="text-emerald-300">Allow</span>
                : <span className="text-red-300">Deny</span>
              : <span className="text-white/40">Disabled</span>}
            </div>
          </div>
        </div>

        <form onSubmit={handleSubmit(onSubmit)} className="grid gap-4 lg:grid-cols-2">
          <div className="space-y-2 lg:col-span-2">
            <label className="block text-sm font-medium text-white">Permission</label>
            <input type="hidden" {...register('permission_qualified', { required: 'Permission is required' })} />

            <div className="relative">
              <input
                value={isPermissionOpen ? permissionSearch : (selectedPermission?.value ?? '')}
                onFocus={() => {
                  setIsPermissionOpen(true)
                  setPermissionSearch('')
                }}
                onChange={e => {
                  setPermissionSearch(e.target.value)
                  setIsPermissionOpen(true)
                }}
                placeholder="Search vault.fs permissions..."
                className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white placeholder:text-white/35"
              />

              {isPermissionOpen && (
                <div className="absolute z-30 mt-2 max-h-80 w-full overflow-y-auto rounded-2xl border border-white/10 bg-[#0a0a0a] p-2 shadow-2xl">
                  {filteredPermissionOptions.length ?
                    filteredPermissionOptions.map(option => {
                      const isSelected = option.value === permissionQualified

                      return (
                        <button
                          key={option.value}
                          type="button"
                          onClick={() => {
                            setValue('permission_qualified', option.value, {
                              shouldDirty: true,
                              shouldValidate: true,
                            })
                            setPermissionSearch('')
                            setIsPermissionOpen(false)
                          }}
                          className={`w-full rounded-xl px-3 py-2 text-left transition ${
                            isSelected ? 'bg-cyan-500/15 ring-1 ring-cyan-400/30' : 'hover:bg-white/5'
                          }`}>
                          <div className="flex items-center justify-between gap-3">
                            <div className="min-w-0">
                              <div className="truncate text-sm font-medium text-white">{option.label}</div>
                              <div className="truncate text-xs text-white/45">{option.value}</div>
                            </div>

                            <div className="hidden shrink-0 text-[10px] tracking-[0.16em] text-white/30 uppercase md:block">
                              {option.meta}
                            </div>
                          </div>

                          {option.description && (
                            <div className="mt-1 line-clamp-2 text-xs text-white/50">{option.description}</div>
                          )}
                        </button>
                      )
                    })
                  : <div className="px-3 py-4 text-sm text-white/50">
                      No vault filesystem permissions match that search.
                    </div>
                  }
                </div>
              )}
            </div>
            {errors.permission_qualified && (
              <p className="text-sm text-red-300">{errors.permission_qualified.message}</p>
            )}

            {selectedPermission && (
              <div className="rounded-2xl border border-white/8 bg-black/20 px-4 py-3">
                <div className="text-xs font-semibold text-white/85">{selectedPermission.value}</div>
                <div className="mt-1 text-sm text-white/55">{selectedPermission.description}</div>
              </div>
            )}
          </div>

          <div className="space-y-2">
            <label className="block text-sm font-medium text-white">Enabled</label>

            <button
              type="button"
              onClick={() =>
                setValue('enabled', !enabled, {
                  shouldDirty: true,
                })
              }
              className={`flex w-full items-center justify-between rounded-2xl border p-3 text-left transition ${
                enabled ? 'border-emerald-400/30 bg-emerald-500/10' : 'border-white/15 bg-black/20'
              }`}>
              <span className="text-white">{enabled ? 'Enabled' : 'Disabled'}</span>

              <span
                className={`relative inline-flex h-6 w-11 items-center rounded-full transition ${
                  enabled ? 'bg-emerald-400/60' : 'bg-white/15'
                }`}>
                <span
                  className={`inline-block h-5 w-5 transform rounded-full bg-white transition ${
                    enabled ? 'translate-x-5' : 'translate-x-1'
                  }`}
                />
              </span>
            </button>
          </div>

          <div className="space-y-2">
            <label className="block text-sm font-medium text-white">Effect</label>

            <select
              {...register('effect')}
              className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white">
              <option value="allow">Allow</option>
              <option value="deny">Deny</option>
            </select>
          </div>

          <div className="space-y-2 lg:col-span-2">
            <label className="block text-sm font-medium text-white">Path / Pattern</label>
            <input
              {...register('pattern', {
                validate: value =>
                  isLikelyValidGlobPattern(value)
                  || 'Pattern must be vault-relative and start with /. Example: /docs/** or /finance/*.csv',
              })}
              placeholder="/docs/**"
              className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white placeholder:text-white/35"
            />
            <p className="text-xs text-white/45">
              Optional. Vault-relative absolute path or git-style glob. Examples: <code>/docs/**</code>,{' '}
              <code>/finance/*.csv</code>, <code>/</code>
            </p>

            {patternError && <p className="text-sm text-red-300">{patternError}</p>}
          </div>
        <div className="mt-6 flex flex-wrap items-center justify-end gap-3">
          {onCancelAction && (
            <Button type="button" onClick={onCancelAction} disabled={isSubmitting}>
              Cancel
            </Button>
          )}

          <Button type="button" onClick={handleReset} disabled={isSubmitting}>
            Reset
          </Button>

          <Button type="submit" disabled={isSubmitting || !canSubmit}>
            {isSubmitting ?
              'Saving...'
            : isEditing ?
              'Update Override'
            : 'Create Override'}
          </Button>
        </div>
        </form>
      </div>
    </motion.div>
  )
}
