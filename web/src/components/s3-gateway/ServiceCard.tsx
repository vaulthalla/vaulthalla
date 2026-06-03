'use client'

import ShieldIcon from '@/fa-duotone/shield-check.svg'
import { S3GatewayStatus } from '@/models/s3Gateway'
import { Metric, Section } from './shared'

export function ServiceCard({ status, endpoint }: { status: S3GatewayStatus | null; endpoint: string }) {
  return (
    <Section title="Service" icon={ShieldIcon}>
      <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-6">
        <Metric label="Running" value={status?.running ? 'Yes' : 'No'} tone={status?.running ? 'good' : 'bad'} />
        <Metric label="Configured" value={status?.configured ? 'Yes' : 'No'} tone={status?.configured ? 'good' : 'bad'} />
        <Metric label="Ready" value={status?.ready ? 'Yes' : 'No'} tone={status?.ready ? 'good' : 'bad'} />
        <Metric label="Endpoint" value={endpoint} />
        <Metric label="Sessions" value={status?.active_sessions ?? 0} />
        <Metric label="Requests" value={`${status?.total_requests ?? 0} / ${status?.failed_requests ?? 0}`} />
      </div>
    </Section>
  )
}
