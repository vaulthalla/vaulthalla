export interface FSEntry {
  id: number
  vault_id: number
  parent_id?: number
  name: string
  created_by: number
  created_at: number | string // Unix timestamp seconds, milliseconds, or ISO timestamp
  updated_at: number | string // Unix timestamp seconds, milliseconds, or ISO timestamp
  last_modified_by?: number
  path?: string
}
