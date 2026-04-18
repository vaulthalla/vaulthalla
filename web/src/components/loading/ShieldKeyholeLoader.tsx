import ShieldKeyhole from '@/fa-duotone/shield-keyhole.svg'

export default function ShieldKeyholeLoader() {
  return (
    <div className="flex h-screen w-full flex-col items-center justify-center space-y-4 text-yellow-100">
      <ShieldKeyhole className="h-12 w-12 animate-pulse fill-current text-yellow-400" />
      <p className="text-xl font-semibold tracking-wide">Authenticating...</p>
    </div>
  )
}
