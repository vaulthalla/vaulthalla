import React from 'react'
import Image from 'next/image'
import FileIcon from '@/fa-duotone/file.svg'

const Thumb = React.memo(function Thumb({ src, alt }: { src: string; alt: string }) {
  const [failed, setFailed] = React.useState(false)

  if (failed) return <FileIcon className="text-primary fill-current" />

  return (
    <Image
      src={src}
      alt={alt}
      height={30}
      width={30}
      className="rounded"
      loading="lazy"
      // Big one: if you suspect Next's optimizer/proxy is the one retrying hard,
      // bypass it for these tiny thumbs:
      unoptimized
      onError={() => setFailed(true)}
    />
  )
})

export default Thumb
