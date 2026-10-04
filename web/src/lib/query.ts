'use client'

import {
  QueryClient,
  useMutation,
  useQuery,
  useQueryClient,
  type QueryKey,
  type UseQueryOptions,
} from '@tanstack/react-query'
import { api, onSessionReset } from '@/lib/session'
import type { Command, Payload, Response } from '@/lib/ws/client'
import { isWsError } from '@/lib/ws/errors'

export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 15_000,
      gcTime: 5 * 60_000,
      // Refusals and missing objects won't change on retry; transport hiccups usually do.
      retry: (failures, error) => failures < 2 && isWsError(error, 'disconnected', 'timeout'),
      refetchOnWindowFocus: false,
      // Polling never runs in a hidden tab.
      refetchIntervalInBackground: false,
    },
    mutations: { retry: false },
  },
})

onSessionReset(() => queryClient.clear())

export const wsKey = <C extends Command>(command: C, payload?: Payload<C>): QueryKey =>
  payload === undefined || payload === null ? [command] : [command, payload]

type WsQueryOptions<C extends Command, T> = Omit<UseQueryOptions<Response<C>, Error, T>, 'queryKey' | 'queryFn'>

// One cached, deduplicated ws read. Pass `refetchInterval` to poll (ref-counted across components, paused when hidden).
export function useWs<C extends Command, T = Response<C>>(command: C, payload: Payload<C>, options: WsQueryOptions<C, T> = {}) {
  return useQuery<Response<C>, Error, T>({
    queryKey: wsKey(command, payload),
    queryFn: ({ signal }) => api.send(command, payload, { signal }),
    ...options,
  })
}

export const fetchWs = <C extends Command>(command: C, payload: Payload<C>) =>
  queryClient.fetchQuery({ queryKey: wsKey(command, payload), queryFn: ({ signal }) => api.send(command, payload, { signal }) })

// Drop every cached read of these commands (any payload) so mounted views refetch.
export const invalidate = (...commands: Command[]) =>
  Promise.all(commands.map(command => queryClient.invalidateQueries({ queryKey: [command] })))

interface WsMutationOptions<C extends Command, V> {
  // Builds the ws payload from the mutation input; defaults to passing the input through.
  toPayload?: (input: V) => Payload<C>
  invalidates?: Command[]
  onSuccess?: (data: Response<C>, input: V) => void | Promise<void>
}

export function useWsMutation<C extends Command, V = Payload<C>>(command: C, options: WsMutationOptions<C, V> = {}) {
  const client = useQueryClient()
  return useMutation<Response<C>, Error, V>({
    mutationKey: [command],
    mutationFn: input => api.send(command, options.toPayload ? options.toPayload(input) : (input as unknown as Payload<C>)),
    onSuccess: async (data, input) => {
      await Promise.all((options.invalidates ?? []).map(c => client.invalidateQueries({ queryKey: [c] })))
      await options.onSuccess?.(data, input)
    },
  })
}
