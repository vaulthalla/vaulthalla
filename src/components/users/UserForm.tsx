'use client'

import { useRouter } from 'next/navigation'
import { useForm } from 'react-hook-form'
import { useAuthStore } from '@/stores/authStore'
import { getErrorMessage } from '@/util/handleErrors'
import { useEffect, useState } from 'react'
import { Button } from '@/components/Button'
import * as motion from 'motion/react-client'
import { User } from '@/models/user'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { userRoleOptions } from '@/util/userRoleMap'

type FormData = {
  id: number
  name: string
  email: string
  password: string
  role: string // string key like 'admin', 'super_admin', etc.
  is_active: boolean | string // react-hook-form may give it as string from <select>
}

const UserForm = ({ name }: { name?: string }) => {
  const router = useRouter()
  const { registerUser, updateUser, getUserByName } = useAuthStore()
  const [error, setError] = useState('')
  const [user, setUser] = useState<User | null>(null)
  const isEdit = !!name

  const {
    register,
    handleSubmit,
    formState: { errors, isSubmitting },
    reset,
  } = useForm<FormData>()

  // Fetch user if editing
  useEffect(() => {
    if (isEdit && name) {
      getUserByName({ name })
        .then(fetchedUser => {
          setUser(fetchedUser)
          console.log(fetchedUser)
          reset({ ...fetchedUser, role: fetchedUser.admin_role.name, password: '' })
        })
        .catch(err => {
          setError(getErrorMessage(err) || 'Failed to fetch user')
        })
    }
  }, [name, isEdit, getUserByName, reset])

  const onSubmit = async (data: FormData) => {
    setError('')
    try {
      if (isEdit && user) {
        data.is_active = Boolean(data.is_active)
        await updateUser({ ...data, id: user.id, is_active: Boolean(data.is_active) })
      } else {
        await registerUser(data.name, data.email, data.password, Boolean(data.is_active), data.role)
      }
      router.push('/dashboard/users')
    } catch (err) {
      setError(getErrorMessage(err) || 'Failed to save user')
    }
  }

  if (isEdit && !user) return <CircleNotchLoader />

  return (
    <motion.form
      initial={{ opacity: 0, y: 20 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.3 }}
      onSubmit={handleSubmit(onSubmit)}
      className="mx-auto mt-12 max-w-sm space-y-4 rounded-3xl bg-white p-6 shadow dark:bg-gray-800">
      <h2 className="text-center text-2xl font-bold">{isEdit ? 'Edit User' : 'Add New User'}</h2>

      <input
        type="text"
        placeholder="Enter name"
        {...register('name', { required: 'Name is required' })}
        className="w-full rounded border px-3 py-2 dark:bg-gray-700"
      />
      {errors.name && <p className="text-sm text-red-500">{errors.name.message}</p>}

      <input
        type="email"
        placeholder="Enter email"
        {...register('email', { required: 'Email is required' })}
        className="w-full rounded border px-3 py-2 dark:bg-gray-700"
      />
      {errors.email && <p className="text-sm text-red-500">{errors.email.message}</p>}

      {!isEdit && (
        <>
          <input
            type="password"
            placeholder="Set password"
            {...register('password', {
              required: 'Password is required',
              minLength: { value: 6, message: 'Min 6 characters' },
            })}
            className="w-full rounded border px-3 py-2 dark:bg-gray-700"
          />
          {errors.password && <p className="text-sm text-red-500">{errors.password.message}</p>}
        </>
      )}

      <select
        {...register('role', { required: 'Role is required' })}
        className="w-full rounded border px-3 py-2 dark:bg-gray-700">
        {userRoleOptions.map(role => (
          <option key={role.value} value={role.value}>
            {role.label}
          </option>
        ))}
      </select>
      {errors.role && <p className="text-sm text-red-500">{errors.role?.message}</p>}

      <select {...register('is_active')} className="w-full rounded border px-3 py-2 dark:bg-gray-700">
        <option value="true">Active</option>
        <option value="false">Inactive</option>
      </select>

      {error && <p className="text-sm text-red-500">{error}</p>}

      <Button type="submit" variant="default" disabled={isSubmitting} className="w-full">
        {isSubmitting ?
          isEdit ?
            'Saving...'
          : 'Registering...'
        : isEdit ?
          'Save Changes'
        : 'Add User'}
      </Button>
    </motion.form>
  )
}

export default UserForm
