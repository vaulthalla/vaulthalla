import { create, type StoreApi, type UseBoundStore } from 'zustand'
import { persist } from 'zustand/middleware'
import { getErrorMessage } from '@/util/handleErrors'
import { useWebSocketStore } from '@/stores/useWebSocket'
import { User } from '@/models/user'
import { WSCommandPayload } from '@/util/webSocketCommands'

type AuthStatus = 'unknown' | 'authenticated' | 'refreshing' | 'unauthenticated'

let refreshPromise: Promise<void> | null = null

const clearPersistedAuthState = () => {
  if (typeof window === 'undefined') return

  localStorage.removeItem('vaulthalla-auth')
  localStorage.removeItem('vaulthalla-groups')
  localStorage.removeItem('vaulthalla-vaults')
  localStorage.removeItem('vaulthalla-fs')
  localStorage.removeItem('vaulthalla-api-keys')
  localStorage.removeItem('vaulthalla-permissions')
}

interface AuthState {
  token: string | null
  user: User | null
  status: AuthStatus
  loading: boolean
  error: string | null
  adminPasswordIsDefault: boolean | undefined

  login: (payload: WSCommandPayload<'auth.login'>) => Promise<void>
  registerUser: (name: string, email: string, password: string, is_active: boolean, role: string) => Promise<void>
  updateUser: (payload: WSCommandPayload<'auth.user.update'>) => Promise<void>
  changePassword: (id: number, old_password: string, new_password: string) => Promise<void>
  isUserAuthenticated: () => Promise<boolean>
  logout: () => Promise<void>
  refreshToken: () => Promise<void>
  fetchUser: () => Promise<void>
  getUser: (id: number) => Promise<User | null>
  getUsers: () => Promise<User[]>
  fetchAdminPasswordIsDefault: (force: boolean) => Promise<boolean>
  getUserByName: (payload: WSCommandPayload<'auth.user.get.byName'>) => Promise<User>
  setToken: (token: string | null) => void
  markUnauthenticated: (reason?: string) => void
}

export const useAuthStore: UseBoundStore<StoreApi<AuthState>> = create<AuthState>()(
  persist<AuthState, [], [], Pick<AuthState, 'token' | 'user' | 'adminPasswordIsDefault'>>(
    (set, get) => ({
      token: null,
      user: null,
      status: 'unknown',
      loading: false,
      error: null,
      adminPasswordIsDefault: undefined,

      login: async ({ name, password }) => {
        set({ loading: true, error: null })
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.login', { name, password })
          const token = response.token ?? get().token
          if (!token) throw new Error('Login response did not include an access token')

          set({ token, user: response.user, status: 'authenticated', error: null })
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Login failed' })
          throw err
        } finally {
          set({ loading: false })
        }
      },

      registerUser: async (name, email, password, is_active, role) => {
        set({ loading: true, error: null })
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          await sendCommand('auth.register', { name, email, password, is_active, role })
        } catch (err) {
          const errorMessage = getErrorMessage(err) || 'Registration failed'
          set({ error: errorMessage })
          throw errorMessage
        } finally {
          set({ loading: false })
        }
      },

      logout: async () => {
        get().markUnauthenticated()
        try {
          const sendCommand = useWebSocketStore.getState().sendCommand
          const socket = useWebSocketStore.getState().socket
          await sendCommand('auth.logout', null)

          setTimeout(() => socket?.close(), 100)
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Logout failed' })
        }
      },

      refreshToken: async () => {
        if (refreshPromise) return refreshPromise

        refreshPromise = (async () => {
          set({ loading: true, error: null, status: 'refreshing' })
          try {
            const sendCommand = useWebSocketStore.getState().sendCommand
            const response = await sendCommand('auth.refresh', null)
            const token = response.token ?? get().token
            if (!token) throw new Error('Refresh response did not include an access token')

            set({ token, user: response.user ?? get().user, error: null, status: 'authenticated' })
            console.log('[Auth] Token refreshed')
          } catch (err) {
            const message = getErrorMessage(err) || 'Token refresh failed'
            console.error('[Auth] Token refresh failed', err)
            get().markUnauthenticated(message)
            throw err
          } finally {
            refreshPromise = null
            set({ loading: false })
          }
        })()

        return refreshPromise
      },

      fetchUser: async () => {
        const token = get().token
        if (!token) {
          set({ user: null })
          return
        }

        set({ loading: true, error: null })

        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.me', null)

          set({ user: response.user, status: 'authenticated' })
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Failed to fetch user' })
        } finally {
          set({ loading: false })
        }
      },

      isUserAuthenticated: async () => {
        set({ loading: true, error: null })
        try {
          const sendCommand = useWebSocketStore.getState().sendCommand
          const token = get().token
          if (!token) return false
          const response = await sendCommand('auth.isAuthenticated', { token })

          if (!response.isAuthenticated || !response.user) return false
          set({ user: response.user, status: 'authenticated' })
          return true
        } catch (err) {
          set({ error: getErrorMessage(err), user: null })
          console.log(err)
          return false
        } finally {
          set({ loading: false })
        }
      },

      updateUser: async payload => {
        set({ loading: true, error: null })
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.user.update', payload)

          set(state => ({ user: state.user?.id === payload.id ? { ...state.user, ...response.user } : state.user }))
        } catch (err) {
          set({ error: getErrorMessage(err) || 'User update failed' })
          throw err
        } finally {
          set({ loading: false })
        }
      },

      changePassword: async (id, old_password, new_password) => {
        set({ loading: true, error: null })
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          await sendCommand('auth.user.change_password', { id, old_password, new_password })
          // If we were forcing a default-password change for the current user,
          // trust the successful update and stop re-checking this flag.
          if (get().adminPasswordIsDefault && get().user?.id === id) {
            set({ adminPasswordIsDefault: false })
          }
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Password change failed' })
          throw err
        } finally {
          set({ loading: false })
        }
      },

      getUser: async (id: number) => {
        set({ loading: true, error: null })
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.user.get', { id })

          return response.user
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Failed to fetch user' })
          throw err
        } finally {
          set({ loading: false })
        }
      },

      getUsers: async () => {
        set({ loading: true, error: null })
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.users.list', null)

          return response.users
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Failed to fetch users' })
          throw err
        } finally {
          set({ loading: false })
        }
      },

      fetchAdminPasswordIsDefault: async (force = false) => {
        const cached = get().adminPasswordIsDefault
        if (!force && cached !== undefined) return cached

        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.admin.default_password', null)
          set({ adminPasswordIsDefault: response.isDefault })
          return response.isDefault
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Failed to fetch admin password' })
          throw err
        }
      },

      getUserByName: async ({ name }: WSCommandPayload<'auth.user.get.byName'>) => {
        try {
          await useWebSocketStore.getState().waitForConnection()
          const sendCommand = useWebSocketStore.getState().sendCommand
          const response = await sendCommand('auth.user.get.byName', { name })
          return response.user
        } catch (err) {
          set({ error: getErrorMessage(err) || 'Failed to fetch user by name' })
          throw err
        }
      },

      setToken: (token: string | null) => {
        set({ token, status: token ? 'authenticated' : 'unauthenticated' })
      },

      markUnauthenticated: (reason?: string) => {
        clearPersistedAuthState()
        set({
          token: null,
          user: null,
          status: 'unauthenticated',
          error: reason,
          adminPasswordIsDefault: undefined,
        })
      },
    }),
    {
      name: 'vaulthalla-auth',
      partialize: state => ({
        token: state.token,
        user: state.user,
        adminPasswordIsDefault: state.adminPasswordIsDefault,
      }),
      onRehydrateStorage: () => () => {
        if (!useAuthStore.getState().token) {
          useAuthStore.setState({ status: 'unauthenticated' })
          return
        }

        console.log('[AuthStore] Rehydrated with token. Starting silent refresh...')
        ;(async () => {
          try {
            await useAuthStore.getState().refreshToken()
          } catch (err) {
            console.error('[AuthStore] Silent refresh failed:', err)
            useAuthStore.getState().markUnauthenticated(getErrorMessage(err) || 'Silent refresh failed')
          }
        })()
      },
    },
  ),
)
