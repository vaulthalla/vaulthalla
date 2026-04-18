import Fingerprint from '@/fa-duotone/fingerprint.svg'

export default function FingerprintLoader() {
  return (
    <div className="flex h-screen w-full flex-col items-center justify-center space-y-4 text-cyan-100">
      <Fingerprint className="h-12 w-12 animate-ping fill-current text-cyan-300" />
      <p className="text-xl font-semibold tracking-wide">Verifying Identity...</p>
    </div>
  )
}
