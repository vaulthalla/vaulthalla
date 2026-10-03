import Link from 'next/link'
import { EmptyState } from '@/components/ui/State'
import { Button } from '@/components/ui/Button'

export default function NotFound() {
  return (
    <EmptyState
      title="This page doesn't exist"
      description="The link may be old, or the item was removed."
      action={
        <Button asChild variant="secondary">
          <Link href="/files">Go to your files</Link>
        </Button>
      }
    />
  )
}
