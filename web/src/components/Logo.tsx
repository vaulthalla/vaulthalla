import NextImage from 'next/image'
import VaulthallaLogo from '@/public/vaulthalla-logo.png'
import Link from 'next/link'

const Logo = () => {
  return (
    <Link href="/dashboard" className="m-0">
      <NextImage
        src={VaulthallaLogo}
        alt="Vaulthalla Logo"
        width={100}
        height={100}
        priority
        className="mx-auto mb-4 h-auto w-auto"
      />
    </Link>
  )
}

export default Logo
