import { NextResponse } from 'next/server'

export const dynamic = 'force-dynamic'

const envFlagEnabled = (value: string | undefined) => {
  switch (value) {
    case '1':
    case 'true':
    case 'TRUE':
    case 'yes':
    case 'YES':
    case 'on':
    case 'ON':
      return true
    default:
      return false
  }
}

export async function GET() {
  return NextResponse.json({
    devMode: envFlagEnabled(process.env.VAULTHALLA_WEB_DEV_MODE),
  })
}
