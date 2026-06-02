'use client'

import CopyIcon from '@/fa-duotone/copy.svg'
import TerminalIcon from '@/fa-duotone/terminal.svg'
import { S3GatewayBucketBinding } from '@/models/s3Gateway'
import { buttonClass, Section } from './shared'

export function ClientSnippets({
  endpoint,
  accessKey,
  secretKey,
  buckets,
  onCopy,
}: {
  endpoint: string
  accessKey: string
  secretKey: string
  buckets: S3GatewayBucketBinding[]
  onCopy: (text: string) => void
}) {
  const snippets = [
    {
      title: 'Environment',
      text: `export AWS_ACCESS_KEY_ID=${accessKey}\nexport AWS_SECRET_ACCESS_KEY=${secretKey}\nexport AWS_EC2_METADATA_DISABLED=true`,
    },
    {
      title: 'AWS CLI Direct',
      text: `aws --endpoint-url http://${endpoint} s3 ls\naws --endpoint-url http://${endpoint} s3 cp ./backup.tar s3://${buckets[0]?.bucket_name ?? 'bucket'}/backup.tar`,
    },
    {
      title: 'AWS CLI Public',
      text: `aws configure set s3.addressing_style path\naws --endpoint-url https://vaulthalla.example.com/api/s3 s3 ls\naws --endpoint-url https://vaulthalla.example.com/api/s3 s3 cp ./backup.tar s3://${buckets[0]?.bucket_name ?? 'bucket'}/backup.tar`,
    },
    {
      title: 'MinIO Client',
      text: `mc alias set vaulthalla http://${endpoint} ${accessKey} ${secretKey}\nmc ls vaulthalla/${buckets[0]?.bucket_name ?? 'bucket'}`,
    },
  ]

  return (
    <Section title="Client Setup" icon={TerminalIcon}>
      <div className="grid gap-3 lg:grid-cols-2">
        {snippets.map(snippet => (
          <div key={snippet.title} className="rounded border border-white/10 bg-black/25 p-3">
            <div className="mb-2 flex items-center justify-between gap-2">
              <div className="text-sm font-medium text-white">{snippet.title}</div>
              <button className={buttonClass} type="button" onClick={() => onCopy(snippet.text)}>
                <CopyIcon className="h-4 w-4" />
                Copy
              </button>
            </div>
            <pre className="overflow-x-auto whitespace-pre-wrap rounded bg-black/35 p-3 text-xs text-cyan-50">{snippet.text}</pre>
          </div>
        ))}
      </div>
    </Section>
  )
}
