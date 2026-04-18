import CircleNotch from '@/fa-duotone/circle-notch.svg'

export default function CircleNotchLoader() {
  return (
    <div className="flex h-screen w-full flex-col items-center justify-center space-y-4 text-cyan-100">
      <CircleNotch className="h-12 w-12 animate-spin fill-current text-cyan-400" />
      <p className="text-xl font-semibold tracking-wide">Loading Interface...</p>
    </div>
  )
}
