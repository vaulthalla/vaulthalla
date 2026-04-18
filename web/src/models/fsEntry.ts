export interface FSEntry {
  id: number
  vault_id: number
  parent_id?: number
  name: string
  created_by: number
  created_at: number // Unix timestamp
  updated_at: number // Unix timestamp
  last_modified_by?: number
  path?: string
}
