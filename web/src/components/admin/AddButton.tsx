import Link from 'next/link'
import { Button } from '@/components/Button'
import Plus from '@/fa-regular/plus.svg'

interface AddButtonProps {
  title: string
  href: string
}

export const AddButton = ({ title, href }: AddButtonProps) => (
  <Link href={href}>
    <Button type="button" className="my-4">
      <Plus className="text-secondary mr-2 fill-current" /> {title}
    </Button>
  </Link>
)
