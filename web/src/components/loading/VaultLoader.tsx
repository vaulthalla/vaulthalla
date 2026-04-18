import Vault from '@/fa-duotone/vault.svg'

export default function VaultLoader() {
  return (
    <div className="flex h-screen w-full flex-col items-center justify-center space-y-4 text-cyan-100">
      <Vault className="animate-spin-slow h-12 w-12 fill-current text-cyan-400" />
      <p className="text-xl font-semibold tracking-wide">Decrypting Vault...</p>
    </div>
  )
}
