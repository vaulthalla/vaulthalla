'use client'

import { useRouter } from 'next/navigation'
import { useForm } from 'react-hook-form'
import { useAuthStore } from '@/stores/authStore'
import { getErrorMessage } from '@/util/handleErrors'
import { useEffect, useState } from 'react'
import Logo from '@/public/vaulthalla-logo.png'
import NextImage from 'next/image'
import { Button } from '@/components/Button'
import * as motion from 'motion/react-client'
import { AnimatePresence } from 'motion/react'
import { vaulthallaQuotes } from '@/util/quotes'

interface LoginFormValues {
  name: string
  password: string
}

const LoginForm = () => {
  const router = useRouter()
  const { login } = useAuthStore()
  const [error, setError] = useState('')
  const [randomQuote, setRandomQuote] = useState<string | null>(null)

  const {
    register,
    handleSubmit,
    formState: { errors, isSubmitting },
  } = useForm<LoginFormValues>()

  useEffect(() => {
    const quote = vaulthallaQuotes[Math.floor(Math.random() * vaulthallaQuotes.length)]
    setRandomQuote(quote)
  }, [])

  const onSubmit = async (data: LoginFormValues) => {
    setError('')
    try {
      await login({ name: data.name, password: data.password })
      if (useAuthStore.getState().token) router.push('/dashboard')
      else setError('Login failed')
    } catch (err) {
      setError(getErrorMessage(err) || 'Login failed')
    }
  }

  return (
    <div className="flex flex-col items-center justify-center rounded-4xl bg-blue-800/20 p-8">
      <NextImage src={Logo} alt="Vaulthalla Logo" priority width={200} height={200} className="mb-8 h-auto w-auto" />

      <AnimatePresence>
        {randomQuote && (
          <motion.p
            key={randomQuote}
            initial={{ opacity: 0, y: 10 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -10 }}
            transition={{ duration: 0.5, ease: 'easeOut' }}
            className="mb-6 max-w-sm text-center text-white italic">
            {randomQuote}
          </motion.p>
        )}
      </AnimatePresence>

      <form onSubmit={handleSubmit(onSubmit)} className="mx-auto max-w-sm space-y-4">
        <h2 className="text-center text-2xl font-bold">Login to Vaulthalla</h2>

        <input
          type="text"
          placeholder="Enter your username"
          autoComplete="username"
          {...register('name', { required: 'Username is required' })}
          className="w-full rounded border px-3 py-2 dark:bg-gray-700"
        />
        {errors.name && <p className="text-sm text-red-500">{errors.name.message}</p>}

        <input
          type="password"
          placeholder="Enter your password"
          autoComplete="current-password"
          {...register('password', { required: 'Password is required' })}
          className="w-full rounded border px-3 py-2 dark:bg-gray-700"
        />
        {errors.password && <p className="text-sm text-red-500">{errors.password.message}</p>}

        {error && <p className="text-sm text-red-500">{error}</p>}

        <Button type="submit" variant="default" disabled={isSubmitting} className="mt-2">
          {isSubmitting ? 'Logging in...' : 'Login'}
        </Button>
      </form>
    </div>
  )
}

export default LoginForm
