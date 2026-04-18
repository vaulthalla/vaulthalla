'use client'

import { useEffect, useState } from 'react'
import { useSettingsStore } from '@/stores/settingsStore'
import CircleNotchLoader from '@/components/loading/CircleNotchLoader'
import { Settings } from '@/models/settings'
import SettingsForm from '@/components/settings/SettingsForm'
import { useWebSocketStore } from '@/stores/useWebSocket'

const SettingsPage = () => {
  const [settings, setSettings] = useState<Settings | null>(null)

  useEffect(() => {
    const fetchSettings = async () => {
      await useWebSocketStore.getState().waitForConnection()
      const settings = await useSettingsStore.getState().getSettings()
      if (settings) setSettings(settings)
      else console.error('Failed to fetch settings')
    }

    fetchSettings()
  }, [])

  if (!settings) return <CircleNotchLoader />

  return (
    <div className="min-h-screen overflow-y-auto p-4">
      <h1 className="mb-6 text-center text-4xl font-bold">Manage Settings</h1>
      <p className="mb-8 text-center text-lg text-gray-300">
        Adjust your server settings below. Changes will be reflected in{' '}
        <code className="text-primary bg-gray-800">/etc/vaulthalla/config.yaml</code> upon save.
      </p>
      <SettingsForm {...settings} />
    </div>
  )
}

export default SettingsPage
