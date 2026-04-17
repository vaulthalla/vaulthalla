'use client'

import { useEffect, useMemo, useState } from 'react'
import { useForm, useWatch } from 'react-hook-form'
import * as motion from 'motion/react-client'
import { useRouter } from 'next/navigation'
import OverrideForm, { PermissionOverrideFormData } from '@/components/roles/vault/OverrideForm'
import { Button } from '@/components/Button'
import { Vault } from '@/models/vaults'
import { PermissionPayload } from '@/models/role'
import { useAuthStore } from '@/stores/authStore'
import { useVaultRoleStore } from '@/stores/useVaultRoleStore'
import { User } from '@/models/user'

type AssignUserFormProps = { vault: Vault }

type AssignUserFormData = { user_id: number; role_id: number }

const sectionVariants = { hidden: { opacity: 0, y: 10 }, visible: { opacity: 1, y: 0 } }

export const AssignUserForm = ({ vault }: AssignUserFormProps) => {
  const router = useRouter()
  const { vaultRoles, fetchVaultRoles, addVaultRole } = useVaultRoleStore()
  const [users, setUsers] = useState<User[]>([])
  const [usersLoading, setUsersLoading] = useState(true)
  const [isSubmitting, setIsSubmitting] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [successMessage, setSuccessMessage] = useState<string | null>(null)

  const [userSearch, setUserSearch] = useState('')
  const [roleSearch, setRoleSearch] = useState('')
  const [isUserOpen, setIsUserOpen] = useState(false)
  const [isRoleOpen, setIsRoleOpen] = useState(false)
  const [overrides, setOverrides] = useState<PermissionOverrideFormData[]>([])
  const [overrideFormKey, setOverrideFormKey] = useState(0)

  const {
    register,
    setValue,
    control,
    handleSubmit,
    formState: { errors },
    reset,
  } = useForm<AssignUserFormData>({ defaultValues: { user_id: 0, role_id: 0 } })

  const selectedUserId = useWatch({ control, name: 'user_id' }) ?? 0
  const selectedRoleId = useWatch({ control, name: 'role_id' }) ?? 0

  useEffect(() => {
    let mounted = true

    const init = async () => {
      setUsersLoading(true)
      try {
        const [fetchedUsers] = await Promise.all([
          useAuthStore.getState().getUsers(),
          !vaultRoles.length ? fetchVaultRoles() : Promise.resolve(),
        ])

        if (!mounted) return
        setUsers(fetchedUsers)
      } catch (err) {
        if (!mounted) return
        setError(err instanceof Error ? err.message : 'Failed to load assignment data.')
      } finally {
        if (mounted) setUsersLoading(false)
      }
    }

    void init()
    return () => {
      mounted = false
    }
  }, [fetchVaultRoles, vaultRoles.length])

  const vaultScopedRoles = useMemo(() => vaultRoles.filter(role => role.vault_id === vault.id), [vaultRoles, vault.id])

  const selectedUser = useMemo(() => users.find(user => user.id === selectedUserId) ?? null, [users, selectedUserId])
  const selectedRole = useMemo(
    () => vaultScopedRoles.find(role => role.id === selectedRoleId) ?? null,
    [vaultScopedRoles, selectedRoleId],
  )

  const filteredUsers = useMemo(() => {
    const query = userSearch.trim().toLowerCase()
    if (!query) return users
    return users.filter(
      user =>
        user.name.toLowerCase().includes(query) || user.email.toLowerCase().includes(query) || `${user.id}` === query,
    )
  }, [users, userSearch])

  const filteredRoles = useMemo(() => {
    const query = roleSearch.trim().toLowerCase()
    if (!query) return vaultScopedRoles
    return vaultScopedRoles.filter(
      role =>
        role.name.toLowerCase().includes(query)
        || role.description.toLowerCase().includes(query)
        || `${role.id}` === query,
    )
  }, [vaultScopedRoles, roleSearch])

  const onSubmit = async (formData: AssignUserFormData) => {
    const role = vaultScopedRoles.find(candidate => candidate.id === formData.role_id)
    if (!role) {
      setError('Selected role no longer exists.')
      return
    }

    setError(null)
    setSuccessMessage(null)
    setIsSubmitting(true)

    try {
      await addVaultRole({
        type: 'vault',
        name: role.name,
        description: role.description,
        permissions: role.permissions.map(permission => new PermissionPayload(permission.qualified, permission.value)),
        vault_id: vault.id,
        subject_type: 'user',
        subject_id: formData.user_id,
        ...(overrides.length ? { permission_overrides: overrides } : {}),
      })

      const userName = users.find(user => user.id === formData.user_id)?.name ?? `User ${formData.user_id}`
      setSuccessMessage(`Assigned ${userName} to "${role.name}" on ${vault.name}.`)
      reset({ user_id: 0, role_id: 0 })
      setOverrides([])
      setUserSearch('')
      setRoleSearch('')
      setIsUserOpen(false)
      setIsRoleOpen(false)
      setOverrideFormKey(prev => prev + 1)
      void fetchVaultRoles()
      router.refresh()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to assign vault role.')
    } finally {
      setIsSubmitting(false)
    }
  }

  if (usersLoading) {
    return (
      <div className="rounded-3xl border border-white/10 bg-black/20 p-6 text-white/70">Loading assignment form...</div>
    )
  }

  return (
    <div className="space-y-6">
      <motion.div variants={sectionVariants} initial="hidden" animate="visible" transition={{ duration: 0.2 }}>
        <div className="rounded-3xl border border-white/10 bg-white/5 p-6 shadow">
          <div className="mb-6">
            <div className="mb-2">
              <span className="inline-flex rounded-full border border-cyan-400/20 bg-cyan-500/10 px-2.5 py-1 text-[10px] font-bold tracking-[0.18em] text-cyan-200 uppercase">
                Assign User
              </span>
            </div>
            <h2 className="text-2xl font-semibold text-white">Assign Vault Role</h2>
            <p className="mt-2 text-sm text-white/60">
              Select a user and a vault role for <span className="font-semibold text-white">{vault.name}</span>.
            </p>
          </div>

          <form onSubmit={handleSubmit(onSubmit)} className="grid gap-4 lg:grid-cols-2">
            <input type="hidden" {...register('user_id', { min: { value: 1, message: 'Select a user.' } })} />
            <input type="hidden" {...register('role_id', { min: { value: 1, message: 'Select a vault role.' } })} />

            <div className="space-y-2">
              <label className="block text-sm font-medium text-white">User</label>
              <div className="relative">
                <input
                  value={
                    isUserOpen ? userSearch
                    : selectedUser ?
                      `${selectedUser.name} (${selectedUser.email})`
                    : ''
                  }
                  onFocus={() => {
                    setIsUserOpen(true)
                    setUserSearch('')
                  }}
                  onChange={event => {
                    setUserSearch(event.target.value)
                    setIsUserOpen(true)
                  }}
                  placeholder="Search user by name or email..."
                  className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white placeholder:text-white/35"
                />

                {isUserOpen && (
                  <div className="absolute z-30 mt-2 max-h-80 w-full overflow-y-auto rounded-2xl border border-white/10 bg-[#0a0a0a] p-2 shadow-2xl">
                    {filteredUsers.length ?
                      filteredUsers.map(user => {
                        const isSelected = user.id === selectedUserId
                        return (
                          <button
                            key={user.id}
                            type="button"
                            onClick={() => {
                              setValue('user_id', user.id, { shouldDirty: true, shouldValidate: true })
                              setUserSearch('')
                              setIsUserOpen(false)
                            }}
                            className={`w-full rounded-xl px-3 py-2 text-left transition ${
                              isSelected ? 'bg-cyan-500/15 ring-1 ring-cyan-400/30' : 'hover:bg-white/5'
                            }`}>
                            <div className="truncate text-sm font-medium text-white">{user.name}</div>
                            <div className="truncate text-xs text-white/45">{user.email}</div>
                          </button>
                        )
                      })
                    : <div className="px-3 py-4 text-sm text-white/50">No users match that search.</div>}
                  </div>
                )}
              </div>
              {errors.user_id && <p className="text-sm text-red-300">{errors.user_id.message}</p>}
            </div>

            <div className="space-y-2">
              <label className="block text-sm font-medium text-white">Vault Role</label>
              <div className="relative">
                <input
                  value={
                    isRoleOpen ? roleSearch
                    : selectedRole ?
                      `${selectedRole.name} (${selectedRole.permissions.filter(permission => permission.value).length} enabled)`
                    : ''
                  }
                  onFocus={() => {
                    setIsRoleOpen(true)
                    setRoleSearch('')
                  }}
                  onChange={event => {
                    setRoleSearch(event.target.value)
                    setIsRoleOpen(true)
                  }}
                  placeholder="Search vault roles..."
                  className="w-full rounded-2xl border border-white/20 bg-black/20 p-3 text-white placeholder:text-white/35"
                />

                {isRoleOpen && (
                  <div className="absolute z-30 mt-2 max-h-80 w-full overflow-y-auto rounded-2xl border border-white/10 bg-[#0a0a0a] p-2 shadow-2xl">
                    {filteredRoles.length ?
                      filteredRoles.map(role => {
                        const enabledCount = role.permissions.filter(permission => permission.value).length
                        const isSelected = role.id === selectedRoleId
                        return (
                          <button
                            key={role.id}
                            type="button"
                            onClick={() => {
                              setValue('role_id', role.id, { shouldDirty: true, shouldValidate: true })
                              setRoleSearch('')
                              setIsRoleOpen(false)
                            }}
                            className={`w-full rounded-xl px-3 py-2 text-left transition ${
                              isSelected ? 'bg-cyan-500/15 ring-1 ring-cyan-400/30' : 'hover:bg-white/5'
                            }`}>
                            <div className="truncate text-sm font-medium text-white">{role.name}</div>
                            <div className="mt-1 line-clamp-2 text-xs text-white/45">
                              {role.description || 'No description provided.'}
                            </div>
                            <div className="mt-1 text-[10px] tracking-[0.14em] text-white/35 uppercase">
                              {enabledCount} / {role.permissions.length} enabled
                            </div>
                          </button>
                        )
                      })
                    : <div className="px-3 py-4 text-sm text-white/50">No vault roles found for this vault.</div>}
                  </div>
                )}
              </div>
              {errors.role_id && <p className="text-sm text-red-300">{errors.role_id.message}</p>}
            </div>

            {selectedRole && (
              <div className="rounded-2xl border border-white/8 bg-black/20 px-4 py-3 lg:col-span-2">
                <div className="text-sm font-semibold text-white">{selectedRole.name}</div>
                <p className="mt-1 text-sm text-white/55">{selectedRole.description || 'No role description.'}</p>
              </div>
            )}

            {error && (
              <div className="rounded-2xl border border-red-300/20 bg-red-500/10 px-4 py-3 text-sm text-red-200 lg:col-span-2">
                {error}
              </div>
            )}

            {successMessage && (
              <div className="rounded-2xl border border-emerald-300/20 bg-emerald-500/10 px-4 py-3 text-sm text-emerald-200 lg:col-span-2">
                {successMessage}
              </div>
            )}

            <div className="mt-2 flex flex-wrap items-center justify-end gap-3 lg:col-span-2">
              <Button
                type="button"
                onClick={() => {
                  reset({ user_id: 0, role_id: 0 })
                  setOverrides([])
                  setUserSearch('')
                  setRoleSearch('')
                  setIsUserOpen(false)
                  setIsRoleOpen(false)
                  setError(null)
                  setSuccessMessage(null)
                  setOverrideFormKey(prev => prev + 1)
                }}
                disabled={isSubmitting}>
                Reset
              </Button>

              <Button type="submit" disabled={isSubmitting || !selectedUserId || !selectedRoleId}>
                {isSubmitting ? 'Assigning...' : 'Assign User'}
              </Button>
            </div>
          </form>
        </div>
      </motion.div>

      <OverrideForm
        key={overrideFormKey}
        title="Override Permission (Optional)"
        subtitle="Add a permission override for this assignment. Submit below to stage it before assigning the user."
        onSubmitAction={data => {
          setOverrides(prev => [...prev, { ...data, id: undefined }])
          setOverrideFormKey(prev => prev + 1)
        }}
      />

      {overrides.length > 0 && (
        <motion.div variants={sectionVariants} initial="hidden" animate="visible" transition={{ duration: 0.2 }}>
          <div className="rounded-3xl border border-white/10 bg-white/5 p-6 shadow">
            <h3 className="text-lg font-semibold text-white">Staged Overrides</h3>
            <p className="mt-2 text-sm text-white/60">
              These overrides will be submitted with the assignment when you click Assign User.
            </p>

            <div className="mt-4 space-y-3">
              {overrides.map((override, index) => (
                <div
                  key={`${override.permission_qualified}-${override.pattern}-${index}`}
                  className="rounded-2xl border border-white/10 bg-black/20 px-4 py-3">
                  <div className="text-sm font-semibold text-white">{override.permission_qualified}</div>
                  <div className="mt-1 text-sm text-white/60">
                    {override.effect.toUpperCase()} • {override.enabled ? 'Enabled' : 'Disabled'} • Pattern:{' '}
                    {override.pattern || '/'}
                  </div>
                  <div className="mt-2">
                    <Button
                      type="button"
                      variant="ghost"
                      size="sm"
                      onClick={() => setOverrides(prev => prev.filter((_, overrideIndex) => overrideIndex !== index))}>
                      Remove
                    </Button>
                  </div>
                </div>
              ))}
            </div>
          </div>
        </motion.div>
      )}
    </div>
  )
}
