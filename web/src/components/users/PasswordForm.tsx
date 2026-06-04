'use client'

import { useRouter } from 'next/navigation'
import { useAuthStore } from '@/stores/authStore'
import { useEffect, useState } from 'react'
import { User } from '@/models/user'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { useForm } from 'react-hook-form'
import { zodResolver } from '@hookform/resolvers/zod'
import * as z from 'zod'
import { Button } from '@/components/Button'
import { getErrorMessage } from '@/util/handleErrors'

const schema = z
  .object({
    old_password: z.string().optional(),
    new_password: z.string().min(8, 'New password must be at least 8 characters'),
    confirm_password: z.string(),
  })
  .refine(data => data.new_password === data.confirm_password, {
    message: 'Passwords do not match',
    path: ['confirm_password'],
  })

type PasswordFormInputs = z.infer<typeof schema>

const PasswordForm = ({ name }: { name: string }) => {
  const router = useRouter()
  const changePassword = useAuthStore(state => state.changePassword)
  const getUserByName = useAuthStore(state => state.getUserByName)
  const currentUser = useAuthStore(state => state.user)
  const adminPasswordIsDefault = useAuthStore(state => state.adminPasswordIsDefault)
  const [user, setUser] = useState<User | null>(null)
  const [error, setError] = useState('')

  const {
    register,
    handleSubmit,
    formState: { errors, isSubmitting },
  } = useForm<PasswordFormInputs>({ resolver: zodResolver(schema) })

  useEffect(() => {
    const fetchUser = async () => {
      try {
        const fetchedUser = await getUserByName({ name })
        setUser(fetchedUser)
      } catch (err) {
        console.error('Failed to fetch user:', err)
      }
    }

    fetchUser()
  }, [name, getUserByName])

  const onSubmit = async (data: PasswordFormInputs) => {
    setError('')
    try {
      if (!user || !user.id) throw new Error('User not found')

      const isSelfChange = currentUser?.id === user.id
      if (isSelfChange && !data.old_password) {
        setError('Old password is required')
        return
      }

      await changePassword(user.id, data.new_password, isSelfChange ? data.old_password : undefined)
      router.push('/users')
    } catch (err) {
      setError(getErrorMessage(err) || 'Failed to change password')
    }
  }

  if (!user) return <CircleNotchLoader />

  const isSelfChange = currentUser?.id === user.id

  const AdminDefaultWarning = () =>
    isSelfChange && adminPasswordIsDefault && (
      <div className="bg-black/40 px-2 py-6 text-center">
        <h2 className="text-destructive mb-4 text-3xl">WARNING</h2>
        <h3 className="text-destructive mb-2 text-lg">Default password detected for admin.</h3>
        <h3 className="text-warning">Please set a new password to proceed.</h3>
      </div>
    )

  return (
    <div className="mx-auto mt-12 h-fit max-w-sm space-y-4 rounded-3xl bg-white p-6 shadow dark:bg-gray-800">
      <h2 className="text-2xl font-semibold">
        {isSelfChange ? 'Change Password' : 'Reset Password'} for {user.name}
      </h2>
      <div className="text-gray-300">
        <h3>
          Name: <span className="text-gray-50">{user.name}</span>
        </h3>
        {user.email && (
          <h3>
            Email: <span className="text-gray-50">{user.email}</span>
          </h3>
        )}
      </div>

      <AdminDefaultWarning />

      <form onSubmit={handleSubmit(onSubmit)} className="mt-4 flex max-w-md flex-col gap-4 text-sm">
        <div hidden>
          <input hidden type="text" id="username" autoComplete="username" value={user.id} readOnly />
        </div>
        {isSelfChange && (
          <div>
            <label className="block font-medium text-gray-200">Old Password</label>
            <input
              type="password"
              {...register('old_password')}
              autoComplete="current-password"
              className="focus:ring-primary w-full rounded-lg border border-gray-600 bg-gray-800 px-3 py-2 text-white focus:ring-2 focus:outline-none"
            />
            {errors.old_password && <p className="mt-1 text-red-400">{errors.old_password.message}</p>}
          </div>
        )}

        <div>
          <label className="block font-medium text-gray-200">New Password</label>
          <input
            type="password"
            {...register('new_password')}
            autoComplete="new-password"
            className="focus:ring-primary w-full rounded-lg border border-gray-600 bg-gray-800 px-3 py-2 text-white focus:ring-2 focus:outline-none"
          />
          {errors.new_password && <p className="mt-1 text-red-400">{errors.new_password.message}</p>}
        </div>

        <div>
          <label className="block font-medium text-gray-200">Confirm New Password</label>
          <input
            type="password"
            {...register('confirm_password')}
            autoComplete="new-password"
            className="focus:ring-primary w-full rounded-lg border border-gray-600 bg-gray-800 px-3 py-2 text-white focus:ring-2 focus:outline-none"
          />
          {errors.confirm_password && <p className="mt-1 text-red-400">{errors.confirm_password.message}</p>}
        </div>

        <Button type="submit" disabled={isSubmitting}>
          {isSubmitting ? 'Updating...' : isSelfChange ? 'Change Password' : 'Reset Password'}
        </Button>
        {error && <p className="text-sm text-red-400">{error}</p>}
      </form>
    </div>
  )
}

export default PasswordForm
